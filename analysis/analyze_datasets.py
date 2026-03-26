"""
Per-dataset validation analysis for OMTF HECIN testsets.
Produces:
  - analysis/report_<DS>.txt  : numerical summary for each dataset
  - analysis/<DS>_*.pdf       : per-dataset plots
  - analysis/comparison_*.pdf : cross-dataset comparisons
  - analysis/FULL_REPORT.txt  : combined report for all datasets
"""
import os, sys
os.environ['DISPLAY'] = ''      # prevent ROOT from hanging on missing X

import ROOT
ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from collections import defaultdict

# ── Config ─────────────────────────────────────────────────────────────────
EOS_BASE  = '/eos/user/p/pleguina/omtf_hecin_datasets/testset'
TREE_PATH = 'simOmtfPhase2Digis/OMTFHitsTree'
OUT_DIR   = os.path.dirname(os.path.abspath(__file__))   # same dir as this script

DATASETS = [
    # (tag, label, color, linestyle, category, pt_range_GeV, max_dxy_cm)
    ('S1', 'S1 single μ',              'tab:blue',   '-',  'signal',      (2, 200), 0),
    ('S2', 'S2 displ. μ |dxy|≤50',    'tab:orange', '-',  'signal_disp', (2, 200), 50),
    ('S3', 'S3 di-μ',                  'tab:green',  '-',  'signal',      (2, 100), 0),
    ('S4', 'S4 tri-μ',                 'tab:red',    '-',  'signal',      (5,  80), 0),
    ('S5', 'S5 2-displ. μ |dxy|≤30',  'tab:purple', '-',  'signal_disp', (5, 100), 30),
    ('B1', 'B1 μ + PU200',             'tab:blue',   '--', 'bkg',         (2, 200), 0),
    ('B2', 'B2 displ. μ + PU200',      'tab:orange', '--', 'bkg_disp',    (2, 200), 50),
    ('B3', 'B3 di-μ + PU200',          'tab:green',  '--', 'bkg',         (2, 100), 0),
]
DS_META = {t: (lbl, col, ls, cat, pt_r, max_dxy) for t, lbl, col, ls, cat, pt_r, max_dxy in DATASETS}

CHAR_BRANCHES  = ['muonCharge', 'omtfCharge', 'omtfProcessor',
                  'omtfQuality', 'omtfRefLayer', 'omtfRefHitNum']
FLOAT_BRANCHES = ['muonPt', 'muonEta', 'muonPhi', 'muonPropEta', 'muonPropPhi',
                  'muonDxy', 'muonRho', 'vertexEta', 'vertexPhi',
                  'omtfPt', 'omtfUPt', 'omtfEta', 'omtfPhi', 'deltaEta', 'deltaPhi']
INT_BRANCHES   = ['eventNum', 'muonEvent', 'parentPdgId', 'omtfHwEta',
                  'omtfScore', 'omtfRefHitPhi', 'omtfFiredLayers']
BOOL_BRANCHES  = ['killed']

plt.rcParams.update({
    'figure.dpi': 120, 'axes.labelsize': 11, 'axes.titlesize': 11,
    'legend.fontsize': 8, 'xtick.labelsize': 9, 'ytick.labelsize': 9,
    'axes.grid': True, 'grid.alpha': 0.3,
})


# ── Data loading ────────────────────────────────────────────────────────────
def load_dataset(tag):
    path = f'{EOS_BASE}/{tag}/omtf_hits_{tag}_0.root'
    tf = ROOT.TFile.Open(path)
    if not tf or tf.IsZombie():
        return None
    tree = tf.Get(TREE_PATH)
    if not tree:
        return None
    char_leaves = {b: tree.GetLeaf(b) for b in CHAR_BRANCHES}
    arrays = defaultdict(list)
    for i in range(tree.GetEntries()):
        tree.GetEntry(i)
        for b in FLOAT_BRANCHES: arrays[b].append(float(getattr(tree, b)))
        for b in CHAR_BRANCHES:  arrays[b].append(int(char_leaves[b].GetValue()))
        for b in INT_BRANCHES:   arrays[b].append(int(getattr(tree, b)))
        for b in BOOL_BRANCHES:  arrays[b].append(bool(getattr(tree, b)))
        arrays['nhits'].append(len(tree.hits_r))
    return {k: np.array(v) for k, v in arrays.items()}


# ── Numerical validation ────────────────────────────────────────────────────
def validate_dataset(tag, d, lines):
    lbl, col, ls, cat, (pt_lo, pt_hi), max_dxy = DS_META[tag]
    n = len(d['muonPt'])

    def p(s): lines.append(s); print(s)

    p(f"\n{'='*70}")
    p(f"  DATASET: {tag}  —  {lbl}")
    p(f"{'='*70}")

    # ── 1. Entry count ──────────────────────────────────────────────────────
    p(f"\n[1] ENTRY COUNT")
    p(f"    Total tree entries (OMTF candidates): {n}")
    if n < 10:
        p(f"    ⚠ WARNING: very few entries!")
    else:
        p(f"    OK")

    # ── 2. Gen pT ───────────────────────────────────────────────────────────
    pt = d['muonPt']
    p(f"\n[2] GEN pT  (expected range [{pt_lo}, {pt_hi}] GeV)")
    p(f"    min={pt.min():.2f}  max={pt.max():.2f}  mean={pt.mean():.2f}  median={np.median(pt):.2f}")
    out_of_range = ((pt < pt_lo) | (pt > pt_hi)).sum()
    p(f"    Out of expected range [{pt_lo},{pt_hi}]: {out_of_range}/{n} ({100*out_of_range/n:.1f}%)")
    if out_of_range / n > 0.01:
        p(f"    ⚠ WARNING: >1% entries outside expected pT range")
    else:
        p(f"    OK — all entries within expected pT range")

    # ── 3. Gen η ────────────────────────────────────────────────────────────
    eta = d['muonPropEta']
    p(f"\n[3] OMTF ACCEPTANCE  (expected |η_prop| in [0.82, 1.24])")
    outside_omtf = ((np.abs(eta) < 0.82) | (np.abs(eta) > 1.24)).sum()
    p(f"    η_prop: min={eta.min():.3f}  max={eta.max():.3f}")
    p(f"    Outside OMTF acceptance [0.82,1.24]: {outside_omtf}/{n} ({100*outside_omtf/n:.1f}%)")
    if outside_omtf / n > 0.05:
        p(f"    ⚠ WARNING: >5% outside acceptance")
    else:
        p(f"    OK — within acceptance")

    # ── 4. Displacement ─────────────────────────────────────────────────────
    dxy = d['muonDxy']
    rho = d['muonRho']
    p(f"\n[4] DISPLACEMENT  (max |dxy| expected: {max_dxy} cm)")
    p(f"    dxy: min={dxy.min():.3f}  max={dxy.max():.3f}  mean={dxy.mean():.4f}  std={dxy.std():.3f}")
    p(f"    rho: min={rho.min():.3f}  max={rho.max():.3f}  mean={rho.mean():.4f}")
    if max_dxy == 0:
        prompt_frac = (np.abs(dxy) < 0.5).sum() / n
        p(f"    Prompt (|dxy|<0.5cm): {100*prompt_frac:.1f}%")
        if prompt_frac < 0.90:
            p(f"    ⚠ WARNING: expected prompt muons but {100*(1-prompt_frac):.1f}% have |dxy|>0.5cm")
        else:
            p(f"    OK — prompt as expected")
    else:
        overflow = (np.abs(dxy) > max_dxy * 1.01).sum()
        p(f"    Exceeding |dxy|>{max_dxy}cm: {overflow}/{n}")
        flat_check = np.std(np.abs(dxy)) / (max_dxy / 2)
        p(f"    |dxy| std/expected = {flat_check:.3f}  (≈0.577 for flat dist)")
        if overflow > 0:
            p(f"    ⚠ WARNING: {overflow} entries exceed max |dxy|")
        else:
            p(f"    OK — displacement within limits")

    # ── 5. OMTF reconstruction ──────────────────────────────────────────────
    omtfPt = d['omtfPt']
    matched = omtfPt > 0
    reco_frac = matched.sum() / n
    p(f"\n[5] OMTF RECONSTRUCTION")
    p(f"    Matched (omtfPt>0): {matched.sum()}/{n} = {100*reco_frac:.1f}%")
    if matched.sum() > 0:
        pt_m = omtfPt[matched]
        p(f"    OMTF pT (matched): mean={pt_m.mean():.2f}  median={np.median(pt_m):.2f}  "
          f"min={pt_m.min():.2f}  max={pt_m.max():.2f}")
        # guard against genPt≈0 entries (gun imprecision) that cause inf resolution
        valid = matched & (pt > 0.5)
        res = (omtfPt[valid] - pt[valid]) / pt[valid]
        p(f"    pT resolution (omtfPt-genPt)/genPt  [genPt>0.5 GeV, n={valid.sum()}]:")
        p(f"      mean={res.mean():.3f}  std={res.std():.3f}  median={np.median(res):.3f}")
        p(f"    pT resolution percentiles [5,25,50,75,95]%: "
          f"{np.percentile(res,[5,25,50,75,95]).round(3).tolist()}")
        # charge agreement
        charge_agree = (d['muonCharge'][matched] == d['omtfCharge'][matched]).sum()
        p(f"    Charge agreement: {charge_agree}/{matched.sum()} = "
          f"{100*charge_agree/matched.sum():.1f}%")

    # ── 6. Trigger turn-on ──────────────────────────────────────────────────
    p(f"\n[6] TRIGGER TURN-ON (threshold = 22 GeV)")
    for thr in [10, 15, 22]:
        pass_frac = (omtfPt >= thr).sum() / n
        p(f"    omtfPt ≥ {thr:2d} GeV: {(omtfPt>=thr).sum():4d}/{n} = {100*pass_frac:.1f}%")
    # plateau efficiency (gen pT > 40 GeV)
    high_pt = pt > 40
    if high_pt.sum() > 0:
        plat = (omtfPt[high_pt] >= 22).sum() / high_pt.sum()
        p(f"    Plateau eff (genPt>40, omtfPt≥22): {100*plat:.1f}%  (n={high_pt.sum()})")
    else:
        p(f"    Not enough high-pT entries for plateau estimate")

    # ── 7. OMTF quality & score ─────────────────────────────────────────────
    qual = d['omtfQuality']
    score = d['omtfScore']
    fired = np.array([bin(int(x)).count('1') for x in d['omtfFiredLayers']])
    p(f"\n[7] OMTF QUALITY / SCORE / FIRED LAYERS")
    p(f"    Quality: mean={qual.mean():.2f}  std={qual.std():.2f}  "
      f"mode={int(np.bincount(qual.clip(0)).argmax())}  "
      f"dist={dict(sorted({int(v): int(c) for v,c in zip(*np.unique(qual.clip(0,15), return_counts=True))}.items()))}")
    p(f"    Score:   mean={score.mean():.1f}  std={score.std():.1f}  "
      f"min={score.min()}  max={score.max()}")
    p(f"    Fired layers: mean={fired.mean():.2f}  std={fired.std():.2f}  "
      f"min={fired.min()}  max={fired.max()}")

    # ── 8. Hit multiplicity ─────────────────────────────────────────────────
    nhits = d['nhits']
    p(f"\n[8] HIT MULTIPLICITY")
    p(f"    nhits: mean={nhits.mean():.2f}  std={nhits.std():.2f}  "
      f"min={nhits.min()}  max={nhits.max()}")
    p(f"    Distribution: {dict(sorted({int(v): int(c) for v,c in zip(*np.unique(nhits, return_counts=True)) if c>0}.items()))}")

    # ── 9. Track killer ─────────────────────────────────────────────────────
    killed_frac = d['killed'].mean()
    p(f"\n[9] TRACK KILLER")
    p(f"    Killed fraction: {d['killed'].sum()}/{n} = {100*killed_frac:.2f}%")
    p(f"    NOTE: 0% is expected — DataROOTDumper2 only writes surviving candidates.")
    p(f"    Killed candidates are excluded (no procMuon → missing processor/eta).")

    # ── 10. Matching (ΔEta, ΔPhi) ───────────────────────────────────────────
    deta = d['deltaEta']
    dphi = d['deltaPhi']
    p(f"\n[10] OMTF MATCHING  (ΔEta, ΔPhi)")
    p(f"    ΔEta: mean={deta.mean():.4f}  std={deta.std():.4f}  "
      f"min={deta.min():.3f}  max={deta.max():.3f}")
    p(f"    ΔPhi: mean={dphi.mean():.4f}  std={dphi.std():.4f}  "
      f"min={dphi.min():.3f}  max={dphi.max():.3f}")
    large_deta = (np.abs(deta) > 0.3).sum()
    large_dphi = (np.abs(dphi) > 0.3).sum()
    p(f"    |ΔEta|>0.3: {large_deta} ({100*large_deta/n:.1f}%)  "
      f"|ΔPhi|>0.3: {large_dphi} ({100*large_dphi/n:.1f}%)")

    # ── 11. parentPdgId ─────────────────────────────────────────────────────
    pdg = d['parentPdgId']
    unique_pdg, counts_pdg = np.unique(pdg, return_counts=True)
    p(f"\n[11] PARENT PDG ID")
    for pid, cnt in sorted(zip(unique_pdg.tolist(), counts_pdg.tolist()), key=lambda x: -x[1])[:8]:
        p(f"    pdgId={pid:6d}: {cnt:5d} ({100*cnt/n:.1f}%)")

    return lines


# ── Per-dataset plots ───────────────────────────────────────────────────────
def plot_dataset(tag, d):
    lbl, col, ls, cat, (pt_lo, pt_hi), max_dxy = DS_META[tag]
    matched = d['omtfPt'] > 0

    fig, axes = plt.subplots(3, 3, figsize=(16, 12))
    axes = axes.flatten()
    fig.suptitle(f'Dataset {tag}: {lbl}', fontsize=13, fontweight='bold')

    # 1. Gen pT
    ax = axes[0]
    ax.hist(d['muonPt'], bins=50, color=col, alpha=0.7, edgecolor='black', lw=0.5)
    ax.set_xlabel('Gen pT [GeV]'); ax.set_title('Gen pT spectrum')
    ax.set_ylabel('Entries')

    # 2. Gen η propagated
    ax = axes[1]
    ax.hist(d['muonPropEta'], bins=50, range=(-1.5, 1.5), color=col, alpha=0.7, edgecolor='black', lw=0.5)
    for xv in [0.82, 1.24, -0.82, -1.24]:
        ax.axvline(xv, color='red', lw=1, ls='--', alpha=0.7)
    ax.set_xlabel('η (propagated)'); ax.set_title('Gen η (OMTF acceptance dashed)')

    # 3. dxy
    ax = axes[2]
    ax.hist(d['muonDxy'], bins=60, color=col, alpha=0.7, edgecolor='black', lw=0.5)
    if max_dxy > 0:
        for xv in [max_dxy, -max_dxy]:
            ax.axvline(xv, color='red', lw=1, ls='--', alpha=0.7)
    ax.set_xlabel('dxy [cm]'); ax.set_title('Gen dxy displacement')

    # 4. OMTF pT vs Gen pT
    ax = axes[3]
    mx = max(d['muonPt'].max(), d['omtfPt'].max()) * 1.05
    ax.hexbin(d['muonPt'][matched], d['omtfPt'][matched],
              gridsize=30, cmap='plasma', mincnt=1, extent=[0, mx, 0, mx])
    ax.plot([0, mx], [0, mx], 'w--', lw=1)
    ax.set_xlabel('Gen pT [GeV]'); ax.set_ylabel('OMTF pT [GeV]')
    ax.set_title(f'OMTF pT vs Gen pT (matched n={matched.sum()})')

    # 5. pT resolution
    ax = axes[4]
    if matched.sum() > 5:
        valid_m = matched & (d['muonPt'] > 0.5)
        res = (d['omtfPt'][valid_m] - d['muonPt'][valid_m]) / d['muonPt'][valid_m]
        ax.hist(res, bins=60, range=(-2, 6), color=col, alpha=0.7, edgecolor='black', lw=0.5)
        ax.axvline(0, color='red', lw=1, ls='--')
        ax.axvline(np.median(res), color='black', lw=1, ls=':', label=f'median={np.median(res):.2f}')
        ax.legend()
    ax.set_xlabel('(OMTF pT - Gen pT) / Gen pT'); ax.set_title('pT resolution')

    # 6. OMTF quality distribution
    ax = axes[5]
    ax.hist(d['omtfQuality'].clip(0, 15), bins=np.arange(-0.5, 17),
            color=col, alpha=0.7, edgecolor='black', lw=0.5)
    ax.set_xlabel('OMTF quality (0–15)'); ax.set_title('OMTF quality')

    # 7. Fired layers
    ax = axes[6]
    fired = np.array([bin(int(x)).count('1') for x in d['omtfFiredLayers']])
    ax.hist(fired, bins=np.arange(-0.5, 20), color=col, alpha=0.7, edgecolor='black', lw=0.5)
    ax.set_xlabel('Fired layers (popcount)'); ax.set_title('Fired OMTF layers')
    ax.set_ylabel('Entries')

    # 8. Hit multiplicity
    ax = axes[7]
    ax.hist(d['nhits'], bins=np.arange(-0.5, 16), color=col, alpha=0.7, edgecolor='black', lw=0.5)
    ax.set_xlabel('Hits per candidate'); ax.set_title(f'Hit multiplicity (mean={d["nhits"].mean():.1f})')

    # 9. ΔEta vs ΔPhi scatter
    ax = axes[8]
    ax.scatter(d['deltaEta'][:2000], d['deltaPhi'][:2000], alpha=0.15, s=3, color=col)
    ax.axhline(0, color='red', lw=0.8); ax.axvline(0, color='red', lw=0.8)
    ax.set_xlabel('ΔEta'); ax.set_ylabel('ΔPhi'); ax.set_title('Matching ΔEta vs ΔPhi')
    ax.set_xlim(-0.5, 0.5); ax.set_ylim(-0.5, 0.5)

    plt.tight_layout()
    out = os.path.join(OUT_DIR, f'{tag}_per_dataset.pdf')
    plt.savefig(out, bbox_inches='tight')
    plt.close()
    print(f"  → saved {out}")


# ── Comparison plots ────────────────────────────────────────────────────────
def plot_comparisons(data):
    LOADED = [t for t, *_ in DATASETS if t in data]

    # No-PU vs PU200 side-by-side for 3 pairs
    pairs = [(n,b) for n,b in [('S1','B1'),('S2','B2'),('S3','B3')] if n in data and b in data]
    if pairs:
        fig, axes = plt.subplots(len(pairs), 4, figsize=(18, 4.5*len(pairs)))
        if len(pairs) == 1: axes = [axes]
        for row, (noPU, PU) in enumerate(pairs):
            for ax, (branch, xlabel, bins) in zip(axes[row], [
                ('nhits',     'N hits',          np.arange(-0.5, 16)),
                ('omtfScore', 'OMTF score',      50),
                ('omtfPt',    'OMTF pT [GeV]',   np.linspace(0, 200, 40)),
                ('omtfQuality','OMTF quality',    np.arange(-0.5, 17)),
            ]):
                for tag, plabel in [(noPU,'no PU'),(PU,'PU200')]:
                    lbl, col, ls, *_ = DS_META[tag]
                    ax.hist(data[tag][branch].astype(float), bins=bins, histtype='step',
                            label=f'{tag} ({plabel})', color=col, linestyle=ls,
                            linewidth=1.8, density=True)
                ax.set_xlabel(xlabel); ax.set_ylabel('Density')
                ax.set_title(f'{noPU} vs {PU}: {xlabel}')
                ax.legend(fontsize=8)
        plt.suptitle('No-PU vs PU200 comparison', fontsize=13)
        plt.tight_layout()
        out = os.path.join(OUT_DIR, 'comparison_PU_vs_noPU.pdf')
        plt.savefig(out, bbox_inches='tight'); plt.close()
        print(f"  → saved {out}")

    # Efficiency turn-on for all datasets
    fig, ax = plt.subplots(figsize=(10, 5))
    pt_edges = np.array([0, 5, 10, 15, 18, 22, 26, 30, 40, 55, 75, 110, 160, 210])
    pt_centers = 0.5 * (pt_edges[:-1] + pt_edges[1:])
    for tag in LOADED:
        lbl, col, ls, *_ = DS_META[tag]
        d = data[tag]
        effs = []
        for lo, hi in zip(pt_edges[:-1], pt_edges[1:]):
            sel = (d['muonPt'] >= lo) & (d['muonPt'] < hi)
            ntot = sel.sum()
            npass = ((d['omtfPt'] >= 22) & sel).sum()
            effs.append(npass/ntot if ntot > 0 else np.nan)
        effs = np.array(effs)
        ok = ~np.isnan(effs)
        ax.plot(pt_centers[ok], effs[ok], label=lbl, color=col, linestyle=ls,
                linewidth=1.8, marker='o', markersize=4)
    ax.axvline(22, color='gray', ls=':', label='22 GeV threshold')
    ax.axhline(1.0, color='lightgray', lw=0.8)
    ax.set_xlabel('Gen pT [GeV]'); ax.set_ylabel('ε (omtfPt ≥ 22 GeV)')
    ax.set_title('Trigger turn-on curves (threshold = 22 GeV)')
    ax.set_ylim(-0.05, 1.15); ax.legend(ncol=2)
    out = os.path.join(OUT_DIR, 'comparison_turnon.pdf')
    plt.savefig(out, bbox_inches='tight'); plt.close()
    print(f"  → saved {out}")

    # Resolution comparison: displaced vs prompt
    fig, axes = plt.subplots(1, 2, figsize=(13, 4))
    for ax, variant, title in [
        (axes[0], ['S1','S2','S5'], 'pT resolution: prompt vs displaced (no PU)'),
        (axes[1], ['B1','B2'], 'pT resolution: prompt vs displaced (PU200)'),
    ]:
        for tag in [t for t in variant if t in data]:
            lbl, col, ls, *_ = DS_META[tag]
            d = data[tag]
            m = (d['omtfPt'] > 0) & (d['muonPt'] > 0)
            if m.sum() < 5: continue
            res = (d['omtfPt'][m] - d['muonPt'][m]) / d['muonPt'][m]
            ax.hist(res, bins=60, range=(-2, 6), histtype='step',
                    label=lbl, color=col, linestyle=ls, linewidth=1.8, density=True)
        ax.axvline(0, color='gray', ls=':', lw=1)
        ax.set_xlabel('(omtfPt - genPt) / genPt')
        ax.set_ylabel('Density'); ax.set_title(title); ax.legend()
    out = os.path.join(OUT_DIR, 'comparison_resolution.pdf')
    plt.savefig(out, bbox_inches='tight'); plt.close()
    print(f"  → saved {out}")

    # Quality vs category bar summary
    fig, ax = plt.subplots(figsize=(10, 4))
    tags = LOADED
    mean_qual = [data[t]['omtfQuality'].mean() for t in tags]
    mean_hits = [data[t]['nhits'].mean() for t in tags]
    x = np.arange(len(tags))
    bars = ax.bar(x - 0.2, mean_qual, width=0.35, label='Mean quality',
                  color=[DS_META[t][1] for t in tags], alpha=0.8)
    ax.bar(x + 0.2, mean_hits, width=0.35, label='Mean nhits',
           color=[DS_META[t][1] for t in tags], alpha=0.4, edgecolor='black')
    ax.set_xticks(x); ax.set_xticklabels([DS_META[t][0] for t in tags])
    ax.set_ylabel('Mean value')
    ax.set_title('Mean OMTF quality and hit count per dataset')
    ax.legend()
    out = os.path.join(OUT_DIR, 'comparison_quality_hits.pdf')
    plt.savefig(out, bbox_inches='tight'); plt.close()
    print(f"  → saved {out}")


# ── Main ────────────────────────────────────────────────────────────────────
def main():
    print(f"\nOutput directory: {OUT_DIR}\n")

    # Load all datasets
    data = {}
    for tag, *_ in DATASETS:
        print(f"Loading {tag}...", end=' ', flush=True)
        d = load_dataset(tag)
        if d is None: print("MISSING"); continue
        data[tag] = d
        print(f"{len(d['muonPt'])} entries")

    if not data:
        print("ERROR: no datasets loaded")
        sys.exit(1)

    all_lines = []
    all_lines.append("OMTF HECIN TESTSET — FULL VALIDATION REPORT")
    all_lines.append(f"Datasets loaded: {list(data.keys())}\n")

    # Per-dataset numerical report + plots
    for tag in data:
        tag_lines = []
        validate_dataset(tag, data[tag], tag_lines)
        all_lines.extend(tag_lines)

        print(f"\nGenerating per-dataset plot for {tag}...")
        plot_dataset(tag, data[tag])

        # Write per-dataset report
        rpt_path = os.path.join(OUT_DIR, f'report_{tag}.txt')
        with open(rpt_path, 'w') as f:
            f.write('\n'.join(tag_lines))
        print(f"  → saved {rpt_path}")

    # Comparison plots
    print("\nGenerating comparison plots...")
    plot_comparisons(data)

    # Full summary table
    LOADED = [t for t, *_ in DATASETS if t in data]
    summary = []
    summary.append("\n\n" + "="*90)
    summary.append("CROSS-DATASET SUMMARY TABLE")
    summary.append("="*90)
    hdr = (f"{'DS':<6} {'N':>6} {'GenPt':>8} {'|dxy|':>8} {'OMTF%':>7} "
           f"{'OMTFPt':>8} {'pTres':>7} {'Quality':>8} {'Hits':>7} {'Killed%':>8} {'ChargeAgr%':>10}")
    summary.append(hdr)
    summary.append('-' * len(hdr))
    for tag in LOADED:
        d = data[tag]
        n = len(d['muonPt'])
        m = d['omtfPt'] > 0
        mv = m & (d['muonPt'] > 0.5)  # guard against genPt≈0
        res = ((d['omtfPt'][mv] - d['muonPt'][mv]) / d['muonPt'][mv]).mean() if mv.sum() > 0 else float('nan')
        chg = (d['muonCharge'][m] == d['omtfCharge'][m]).mean() * 100 if m.sum() > 0 else float('nan')
        summary.append(
            f"{tag:<6} {n:>6} {d['muonPt'].mean():>8.1f} {np.abs(d['muonDxy']).mean():>8.3f} "
            f"{100*m.mean():>6.1f}% {d['omtfPt'][m].mean() if m.sum()>0 else 0:>8.1f} "
            f"{res:>7.3f} {d['omtfQuality'].mean():>8.2f} {d['nhits'].mean():>7.2f} "
            f"{100*d['killed'].mean():>7.1f}% {chg:>9.1f}%"
        )
    all_lines.extend(summary)

    for s in summary:
        print(s)

    # Write full report
    full_path = os.path.join(OUT_DIR, 'FULL_REPORT.txt')
    with open(full_path, 'w') as f:
        f.write('\n'.join(all_lines))
    print(f"\n\nFull report saved to: {full_path}")
    print("Done.")


if __name__ == '__main__':
    main()
