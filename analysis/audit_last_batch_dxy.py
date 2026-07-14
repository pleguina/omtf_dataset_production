#!/usr/bin/env python3
import glob
import json
import math
import os
import time
from collections import Counter

import awkward as ak
import numpy as np
import uproot

BASE = "/eos/user/p/pleguina/omtf_hecin_datasets/prod"
OUT_JSON = "/afs/cern.ch/user/p/pleguina/omtf_dataset_production/analysis/last_batch_dxy_variable_audit_20260712.json"
OUT_MD = "/afs/cern.ch/user/p/pleguina/omtf_dataset_production/analysis/last_batch_dxy_variable_audit_20260712.md"

DATASETS = [
    "C13_prompt_pt2to5_overlap_PU200",
    "C14_prompt_pt5to10_overlap_PU200",
    "C15_disp_pt2to5_overlap_PU200",
    "C16_disp_pt5to10_overlap_PU200",
    "C17_prompt_pt10to20_overlap_PU200",
    "C18_prompt_pt20to50_overlap_PU200",
    "C19_prompt_pt50to100_overlap_PU200",
    "C20_prompt_pt100to200_overlap_PU200",
    "C21_disp_pt10to20_overlap_PU200",
    "C22_disp_pt20to50_overlap_PU200",
    "C23_disp_pt50to100_overlap_PU200",
    "C24_disp_pt100to200_overlap_PU200",
    "C27_mild_disp_pt2to5_overlap_PU200",
    "C28_mild_disp_pt5to10_overlap_PU200",
]

# Keep this modest so the audit is fast but still checks all files.
ENTRY_STOP_PER_FILE = 300

REQ_BRANCHES = [
    "GenMuon_pt",
    "GenMuon_charge",
    "GenMuon_phi",
    "GenMuon_vx",
    "GenMuon_vy",
    "GenMuon_dXY",
    "GenMuon_lXY",
    "GenMuon_eta",
    "Pileup_nPU",
    "Pileup_nTrueInt",
    "nomtf",
    "omtf_hwQual",
    "omtf_hwPt",
    "omtf_hwDXY",
]

B_FIELD = 3.811
K_FACTOR = 0.003 * B_FIELD
DXY_FIX_CUTOFF = time.mktime(time.strptime("2026-07-10 12:00:00", "%Y-%m-%d %H:%M:%S"))


def contiguous_ranges(indices):
    if not indices:
        return []
    out = []
    s = indices[0]
    p = indices[0]
    for x in indices[1:]:
        if x == p + 1:
            p = x
        else:
            out.append((s, p))
            s = x
            p = x
    out.append((s, p))
    return out


def recompute_dxy_true(pt, charge, phi, vx, vy):
    rg = -pt / (K_FACTOR * charge)
    cx = vx - rg * np.sin(phi)
    cy = vy + rg * np.cos(phi)
    return rg + charge * np.sqrt(cx * cx + cy * cy)


def summarize_np(a):
    if a.size == 0:
        return None
    return {
        "count": int(a.size),
        "mean": float(np.mean(a)),
        "std": float(np.std(a)),
        "min": float(np.min(a)),
        "p1": float(np.percentile(a, 1)),
        "p50": float(np.percentile(a, 50)),
        "p95": float(np.percentile(a, 95)),
        "p99": float(np.percentile(a, 99)),
        "max": float(np.max(a)),
    }


def concat_or_empty(parts):
    if not parts:
        return np.array([])
    non_empty = [x for x in parts if getattr(x, "size", 0) > 0]
    if not non_empty:
        return np.array([])
    return np.concatenate(non_empty)


report = {
    "generated_at": time.strftime("%Y-%m-%d %H:%M:%S"),
    "base": BASE,
    "entry_stop_per_file": ENTRY_STOP_PER_FILE,
    "datasets": {},
    "notes": [],
}

for ds in DATASETS:
    dpath = os.path.join(BASE, ds)
    files = sorted(glob.glob(os.path.join(dpath, "omtf_nano_*.root")))

    if not files:
        report["datasets"][ds] = {"error": "no files found"}
        continue

    idxs = []
    mtimes = []
    for f in files:
        bn = os.path.basename(f)
        try:
            idx = int(bn.rsplit("_", 1)[1].replace(".root", ""))
        except Exception:
            idx = -1
        idxs.append(idx)
        mtimes.append(os.path.getmtime(f))

    idxs_sorted = sorted(i for i in idxs if i >= 0)

    ds_sum = {
        "n_files": len(files),
        "index_min": min(idxs_sorted) if idxs_sorted else None,
        "index_max": max(idxs_sorted) if idxs_sorted else None,
        "index_ranges": contiguous_ranges(idxs_sorted),
        "mtime_min": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(min(mtimes))),
        "mtime_max": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(max(mtimes))),
        "events_scanned": 0,
        "branch_presence": {},
        "vars": {},
        "quality": {},
        "dxy": {},
        "sample_file_dxy": [],
    }

    presence = Counter()
    vars_parts = {
        "GenMuon_pt": [],
        "GenMuon_lXY": [],
        "GenMuon_eta": [],
        "GenMuon_dXY": [],
        "Pileup_nPU": [],
        "Pileup_nTrueInt": [],
        "nomtf": [],
        "omtf_hwQual": [],
        "omtf_hwPt": [],
        "omtf_hwDXY": [],
    }

    abs_diffs = []
    signed_diffs = []
    rel_diffs = []

    by_age = {
        "older": {"file_mae": [], "file_p95": [], "n_gen": 0},
        "newer": {"file_mae": [], "file_p95": [], "n_gen": 0},
    }

    for i, f in enumerate(files):
        try:
            with uproot.open(f) as rf:
                if "Events" not in rf:
                    continue
                tree = rf["Events"]
                keys = set(tree.keys())
                for b in REQ_BRANCHES:
                    if b in keys:
                        presence[b] += 1

                want = [b for b in REQ_BRANCHES if b in keys]
                arr = tree.arrays(want, library="ak", entry_stop=ENTRY_STOP_PER_FILE)
                ds_sum["events_scanned"] += int(min(tree.num_entries, ENTRY_STOP_PER_FILE))

                for b in ["Pileup_nPU", "Pileup_nTrueInt", "nomtf"]:
                    if b in arr.fields:
                        vars_parts[b].append(ak.to_numpy(arr[b]))

                for b in ["omtf_hwQual", "omtf_hwPt", "omtf_hwDXY"]:
                    if b in arr.fields:
                        vars_parts[b].append(ak.to_numpy(ak.flatten(arr[b], axis=None)))

                gm = ["GenMuon_pt", "GenMuon_charge", "GenMuon_phi", "GenMuon_vx", "GenMuon_vy", "GenMuon_dXY", "GenMuon_lXY", "GenMuon_eta"]
                if all(b in arr.fields for b in gm):
                    pt = np.asarray(ak.to_numpy(ak.flatten(arr["GenMuon_pt"], axis=None)), dtype=np.float64)
                    ch = np.asarray(ak.to_numpy(ak.flatten(arr["GenMuon_charge"], axis=None)), dtype=np.float64)
                    ph = np.asarray(ak.to_numpy(ak.flatten(arr["GenMuon_phi"], axis=None)), dtype=np.float64)
                    vx = np.asarray(ak.to_numpy(ak.flatten(arr["GenMuon_vx"], axis=None)), dtype=np.float64)
                    vy = np.asarray(ak.to_numpy(ak.flatten(arr["GenMuon_vy"], axis=None)), dtype=np.float64)
                    dxy = np.asarray(ak.to_numpy(ak.flatten(arr["GenMuon_dXY"], axis=None)), dtype=np.float64)
                    lxy = np.asarray(ak.to_numpy(ak.flatten(arr["GenMuon_lXY"], axis=None)), dtype=np.float64)
                    eta = np.asarray(ak.to_numpy(ak.flatten(arr["GenMuon_eta"], axis=None)), dtype=np.float64)

                    vars_parts["GenMuon_pt"].append(pt)
                    vars_parts["GenMuon_lXY"].append(lxy)
                    vars_parts["GenMuon_eta"].append(eta)
                    vars_parts["GenMuon_dXY"].append(dxy)

                    mask = (ch != 0) & np.isfinite(pt) & np.isfinite(ch) & np.isfinite(ph) & np.isfinite(vx) & np.isfinite(vy) & np.isfinite(dxy)
                    if np.any(mask):
                        dxy_true = recompute_dxy_true(pt[mask], ch[mask], ph[mask], vx[mask], vy[mask])
                        diff = dxy[mask] - dxy_true
                        ad = np.abs(diff)
                        rd = np.abs(diff) / (np.abs(dxy_true) + 1e-6)

                        abs_diffs.append(ad)
                        signed_diffs.append(diff)
                        rel_diffs.append(rd)

                        grp = "newer" if os.path.getmtime(f) >= DXY_FIX_CUTOFF else "older"
                        by_age[grp]["file_mae"].append(float(np.mean(ad)))
                        by_age[grp]["file_p95"].append(float(np.percentile(ad, 95)))
                        by_age[grp]["n_gen"] += int(ad.size)

                        if i < 20:
                            ds_sum["sample_file_dxy"].append(
                                {
                                    "file": os.path.basename(f),
                                    "mtime": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(f))),
                                    "n_gen": int(ad.size),
                                    "mae_abs_diff_cm": float(np.mean(ad)),
                                    "p95_abs_diff_cm": float(np.percentile(ad, 95)),
                                }
                            )
        except Exception as exc:
            ds_sum.setdefault("file_errors", []).append({"file": os.path.basename(f), "error": str(exc)})

    ds_sum["branch_presence"] = {
        b: {"files_with_branch": int(presence[b]), "total_files": len(files)} for b in REQ_BRANCHES
    }

    for k in ["GenMuon_pt", "GenMuon_lXY", "GenMuon_eta", "GenMuon_dXY", "Pileup_nPU", "Pileup_nTrueInt"]:
        ds_sum["vars"][k] = summarize_np(concat_or_empty(vars_parts[k]))

    for k in ["nomtf", "omtf_hwQual", "omtf_hwPt", "omtf_hwDXY"]:
        ds_sum["quality"][k] = summarize_np(concat_or_empty(vars_parts[k]))

    ad = concat_or_empty(abs_diffs)
    sd = concat_or_empty(signed_diffs)
    rr = concat_or_empty(rel_diffs)

    if ad.size > 0:
        ds_sum["dxy"] = {
            "n_gen_evaluated": int(ad.size),
            "mae_abs_diff_cm": float(np.mean(ad)),
            "median_abs_diff_cm": float(np.median(ad)),
            "p95_abs_diff_cm": float(np.percentile(ad, 95)),
            "p99_abs_diff_cm": float(np.percentile(ad, 99)),
            "max_abs_diff_cm": float(np.max(ad)),
            "mean_signed_diff_cm": float(np.mean(sd)),
            "frac_absdiff_gt_0p01cm": float(np.mean(ad > 0.01)),
            "frac_absdiff_gt_0p1cm": float(np.mean(ad > 0.1)),
            "frac_absdiff_gt_1cm": float(np.mean(ad > 1.0)),
            "median_rel_diff": float(np.median(rr)),
            "p95_rel_diff": float(np.percentile(rr, 95)),
            "by_filetime_group": {
                g: {
                    "n_gen": int(by_age[g]["n_gen"]),
                    "n_files": int(len(by_age[g]["file_mae"])),
                    "mean_file_mae_cm": float(np.mean(by_age[g]["file_mae"])) if by_age[g]["file_mae"] else None,
                    "mean_file_p95_cm": float(np.mean(by_age[g]["file_p95"])) if by_age[g]["file_p95"] else None,
                }
                for g in ["older", "newer"]
            },
        }
    else:
        ds_sum["dxy"] = {"error": "no dxy-evaluable gen muons found"}

    report["datasets"][ds] = ds_sum

mixed = []
for ds, info in report["datasets"].items():
    dxy = info.get("dxy", {})
    grp = dxy.get("by_filetime_group", {})
    old_mae = grp.get("older", {}).get("mean_file_mae_cm")
    new_mae = grp.get("newer", {}).get("mean_file_mae_cm")
    if old_mae is not None and new_mae is not None and abs(old_mae - new_mae) > 0.05:
        mixed.append((ds, old_mae, new_mae))

report["notes"].append("Potential mixed DXY behavior: |older_mae - newer_mae| > 0.05 cm")
report["notes"].extend([f"{ds}: older={o:.4f}, newer={n:.4f}" for ds, o, n in mixed] or ["none"])

with open(OUT_JSON, "w", encoding="utf-8") as f:
    json.dump(report, f, indent=2)

lines = []
lines.append("# Last Batch Variable + DXY Audit")
lines.append("")
lines.append(f"Generated: {report['generated_at']}")
lines.append(f"Base: `{BASE}`")
lines.append(f"Entry stop per file: {ENTRY_STOP_PER_FILE}")
lines.append("")
lines.append("## Overview")
lines.append("")
lines.append("| Dataset | Files | Index ranges | mtime min | mtime max | Events scanned | Gen dxy evaluated | DXY MAE (cm) | DXY p95 (cm) | frac(|Δdxy|>0.1cm) |")
lines.append("|---|---:|---|---|---|---:|---:|---:|---:|---:|")
for ds in DATASETS:
    info = report["datasets"].get(ds, {})
    if "error" in info:
        lines.append(f"| {ds} | 0 | - | - | - | - | - | - | - | - |")
        continue
    ranges = "; ".join([f"{a}-{b}" if a != b else str(a) for a, b in info.get("index_ranges", [])])
    dxy = info.get("dxy", {})
    mae = dxy.get("mae_abs_diff_cm")
    p95 = dxy.get("p95_abs_diff_cm")
    frac = dxy.get("frac_absdiff_gt_0p1cm")
    lines.append(
        f"| {ds} | {info.get('n_files', 0)} | {ranges} | {info.get('mtime_min', '-')} | {info.get('mtime_max', '-')} | {info.get('events_scanned', 0)} | {dxy.get('n_gen_evaluated', 0)} | {mae:.4f} | {p95:.4f} | {frac:.4f} |"
    )

lines.append("")
lines.append("## Mixed DXY Check")
lines.append("")
lines.append("| Dataset | older mean file MAE (cm) | newer mean file MAE (cm) | older files | newer files |")
lines.append("|---|---:|---:|---:|---:|")
for ds in DATASETS:
    info = report["datasets"].get(ds, {})
    grp = info.get("dxy", {}).get("by_filetime_group", {})
    older = grp.get("older", {})
    newer = grp.get("newer", {})
    lines.append(
        f"| {ds} | {older.get('mean_file_mae_cm')} | {newer.get('mean_file_mae_cm')} | {older.get('n_files', 0)} | {newer.get('n_files', 0)} |"
    )

lines.append("")
lines.append("## Notes")
for note in report["notes"]:
    lines.append(f"- {note}")

with open(OUT_MD, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")

print("WROTE", OUT_JSON)
print("WROTE", OUT_MD)
print("MIXED_DATASETS", len(mixed))
for ds, o, n in mixed:
    print("MIXED", ds, f"older={o:.4f}", f"newer={n:.4f}")
