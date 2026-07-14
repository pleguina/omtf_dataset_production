#!/usr/bin/env python3
from __future__ import annotations

import datetime
import glob
import json
import os
import re
import subprocess
from pathlib import Path
from typing import Optional

import uproot

BASE = Path('/eos/user/p/pleguina/omtf_hecin_datasets/prod')
WORK = Path('/afs/cern.ch/user/p/pleguina/omtf_dataset_production')
CONDOR = WORK / 'condor'
ANALYSIS = WORK / 'analysis'
ANALYSIS.mkdir(parents=True, exist_ok=True)

stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
manifest_path = ANALYSIS / f'full_schema_outdated_manifest_{stamp}.json'
report_path = ANALYSIS / f'full_schema_cleanup_resubmit_report_{stamp}.json'
resub_dir = CONDOR / f'resubmit_fullschema_{stamp}'
resub_dir.mkdir(parents=True, exist_ok=True)

# Known-good full schema reference
ref_file = BASE / 'C28_mild_disp_pt5to10_overlap_PU200' / 'omtf_nano_C28_mild_disp_pt5to10_overlap_PU200_399.root'
with uproot.open(ref_file) as rf:
    ref_keys = set(rf['Events'].keys())


def target_datasets():
    out = []
    for d in sorted(BASE.iterdir()):
        if not d.is_dir():
            continue
        n = d.name
        if n == 'B4' or n.startswith('C') or n.startswith('G'):
            out.append(n)
    return out


def get_template(ds: str) -> Optional[Path]:
    if ds == 'B4':
        p = CONDOR / 'B4_digi.sub'
        return p if p.exists() else None

    exact = CONDOR / f'{ds}.sub'
    if exact.exists():
        return exact

    cands = []
    for p in CONDOR.glob('*.sub'):
        try:
            txt = p.read_text()
        except Exception:
            continue
        if re.search(rf'^\s*arguments\s*=\s*{re.escape(ds)}\s+\$\(ProcId\)', txt, re.M):
            cands.append(p)

    if not cands:
        return None

    def rank(path: Path):
        name = path.name.lower()
        return (
            1 if 'resub' in name else 0,
            1 if 'retry' in name else 0,
            len(name),
        )

    return sorted(cands, key=rank)[0]


def build_resub_submit(ds, idxs, template, out_file):
    txt = template.read_text()
    # Replace queue clause at end with explicit ProcId list
    txt = re.sub(r'(?ms)^\s*queue\b.*$', '', txt).rstrip() + '\n\n'
    txt += 'queue ProcId from (\n'
    for i in sorted(idxs):
        txt += f'{i}\n'
    txt += ')\n'
    out_file.write_text(txt)


def run(cmd, cwd=None):
    return subprocess.run(cmd, cwd=str(cwd) if cwd else None, text=True, capture_output=True)


manifest = {
    'generated_at': datetime.datetime.now().isoformat(),
    'reference_file': str(ref_file),
    'reference_branch_count': len(ref_keys),
    'datasets': {},
    'totals': {'datasets_scanned': 0, 'datasets_affected': 0, 'files_scanned': 0, 'outdated_files': 0},
}

for ds in target_datasets():
    print(f'[scan] dataset={ds}', flush=True)
    files = sorted((BASE / ds).glob('omtf_nano_*.root'))
    if not files:
        continue

    manifest['totals']['datasets_scanned'] += 1
    manifest['totals']['files_scanned'] += len(files)

    outdated = []
    missing_union = set()

    for i, f in enumerate(files, start=1):
        try:
            with uproot.open(f) as rf:
                if 'Events' not in rf:
                    continue
                keys = set(rf['Events'].keys())
        except Exception:
            continue

        if i % 100 == 0:
            print(f'[scan] dataset={ds} checked={i}/{len(files)}', flush=True)

        miss = sorted(ref_keys - keys)
        if miss:
            m = re.search(r'_(\d+)\.root$', f.name)
            idx = int(m.group(1)) if m else -1
            hits = f.with_name(f.name.replace('omtf_nano_', 'omtf_hits_'))
            outdated.append({'index': idx, 'nano': str(f), 'hits': str(hits), 'missing_vars': miss})
            missing_union.update(miss)

    if outdated:
        print(f'[scan] dataset={ds} outdated={len(outdated)}/{len(files)}', flush=True)
        manifest['datasets'][ds] = {
            'total_files': len(files),
            'outdated_count': len(outdated),
            'indices': sorted(x['index'] for x in outdated),
            'missing_vars_union': sorted(missing_union),
            'files': outdated,
        }
        manifest['totals']['datasets_affected'] += 1
        manifest['totals']['outdated_files'] += len(outdated)

manifest_path.write_text(json.dumps(manifest, indent=2))
print(f'[manifest] wrote {manifest_path}', flush=True)

report = {
    'started_at': datetime.datetime.now().isoformat(),
    'manifest': str(manifest_path),
    'deleted': {},
    'resubmits': {},
    'errors': [],
}

# 1) Delete outdated nano/hits pairs
for ds, info in manifest['datasets'].items():
    print(f'[delete] dataset={ds} count={info["outdated_count"]}', flush=True)
    deleted_nano = 0
    deleted_hits = 0
    missing_hits = 0
    for rec in info['files']:
        n = Path(rec['nano'])
        h = Path(rec['hits'])
        if n.exists():
            n.unlink()
            deleted_nano += 1
        if h.exists():
            h.unlink()
            deleted_hits += 1
        else:
            missing_hits += 1
    report['deleted'][ds] = {
        'requested': info['outdated_count'],
        'deleted_nano': deleted_nano,
        'deleted_hits': deleted_hits,
        'missing_hits_before_delete': missing_hits,
    }

# 2) Build and submit per-dataset resub files
for ds, info in manifest['datasets'].items():
    print(f'[submit] dataset={ds}', flush=True)
    idxs = sorted(info['indices'])
    template = get_template(ds)
    if template is None:
        report['errors'].append(f'No submit template found for {ds}')
        continue

    sub_out = resub_dir / f'{ds}_fullschema_resub.sub'
    try:
        build_resub_submit(ds, idxs, template, sub_out)
    except Exception as e:
        report['errors'].append(f'Failed to generate submit for {ds}: {e}')
        continue

    res = run(['condor_submit', str(sub_out)], cwd=WORK)
    cluster = None
    m = re.search(r'cluster\s+(\d+)', (res.stdout or '') + '\n' + (res.stderr or ''), re.I)
    if m:
        cluster = int(m.group(1))

    report['resubmits'][ds] = {
        'template': str(template),
        'submit_file': str(sub_out),
        'jobs': len(idxs),
        'cluster': cluster,
        'returncode': res.returncode,
        'stdout': (res.stdout or '').strip(),
        'stderr': (res.stderr or '').strip(),
    }

report['finished_at'] = datetime.datetime.now().isoformat()
report_path.write_text(json.dumps(report, indent=2))

print('MANIFEST', manifest_path)
print('REPORT', report_path)
print('AFFECTED_DATASETS', manifest['totals']['datasets_affected'])
print('OUTDATED_FILES', manifest['totals']['outdated_files'])
for ds, v in report['resubmits'].items():
    print('SUBMIT', ds, 'jobs=', v['jobs'], 'cluster=', v['cluster'], 'rc=', v['returncode'])
if report['errors']:
    print('ERRORS', len(report['errors']))
    for e in report['errors']:
        print(' -', e)
