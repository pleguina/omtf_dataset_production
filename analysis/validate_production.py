#!/usr/bin/env python3
"""
validate_production.py — Validate ALL OMTF HECIN production dataset files
for GNN model design readiness (based on OMTF_GNN_Topology_Analysis.md).

Run inside CMSSW environment:
    cd /afs/cern.ch/user/p/pleguina/CMSSW_14_2_0_pre2/src
    eval $(scramv1 runtime -sh)
    cd /afs/cern.ch/user/p/pleguina
    python3 omtf_hecin_dataset_production/analysis/validate_production.py

Output (in analysis/):
    prod_report_<DS>.txt        — per-dataset numerical report
    prod_<DS>.pdf               — per-dataset plots
    prod_comparison_PU.pdf      — No-PU vs PU200
    prod_comparison_turnon.pdf  — trigger turn-on all datasets
    prod_comparison_gnn.pdf     — GNN-specific: nhits, fired layers, hit types
    PRODUCTION_VALIDATION.txt   — combined report + GO/NO-GO per dataset
"""
import os, sys, glob

os.environ['DISPLAY'] = ''  # no X server on batch/lxplus

try:
    import ROOT
    ROOT.gROOT.SetBatch(True)
    ROOT.gErrorIgnoreLevel = ROOT.kError
except ModuleNotFoundError:
    sys.exit("ERROR: ROOT not available. Run inside CMSSW (cmsenv).")

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from collections import defaultdict

# ── Config ──────────────────────────────────────────────────────────────────
EOS_BASE  = '/eos/user/p/pleguina/omtf_hecin_datasets/prod'
TREE_PATH = 'simOmtfPhase2Digis/OMTFHitsTree'
OUT_DIR   = os.path.dirname(os.path.abspath(__file__))
MAX_ENTRIES = 3_000_000  # safety cap on entries loaded at once via RDataFrame

# Dataset definitions: tag → (label, colour, linestyle, pT-range, max|dxy| cm)
DATASETS = [
    ('S1', 'S1 single μ',              'tab:blue',   '-',  (2, 200), 0),
    ('S2', 'S2 displ. μ |dxy|≤50 cm', 'tab:orange', '-',  (2, 200), 50),
    ('S3', 'S3 di-μ',                  'tab:green',  '-',  (2, 100), 0),
    ('S4', 'S4 tri-μ',                 'tab:red',    '-',  (5,  80), 0),
    ('S5', 'S5 2-displ. μ |dxy|≤30',  'tab:purple', '-',  (5, 100), 30),
    ('B1', 'B1 μ + PU200',             'tab:blue',   '--', (2, 200), 0),
    ('B2', 'B2 displ. μ + PU200',      'tab:orange', '--', (2, 200), 50),
    ('B3', 'B3 di-μ + PU200',          'tab:green',  '--', (2, 100), 0),
]
DS_META = {t: (lbl, col, ls, pt_r, max_dxy) for t, lbl, col, ls, pt_r, max_dxy in DATASETS}

# Expected total events per dataset (original plan)
EXPECTED_EVENTS = {'S1': 500_000, 'S2': 500_000, 'S3': 500_000,
                   'S4': 150_000, 'S5': 150_000,
                   'B1': 200_000, 'B2': 200_000, 'B3': 200_000}

# hits_type values observed in CMSSW_14_2_0_pre2 OMTF Phase-2
# (from DataROOTDumper2; 3=DT, 5=CSC, 9=RPC based on observed data)
HIT_TYPE_NAMES = {3: 'DT', 5: 'CSC', 9: 'RPC'}

# ── GNN readiness thresholds (from §1.1 and §1.3 of Topology Analysis) ─────
GNN = {
    'nhits_mean_min': 3.5,   # at least 3-4 stubs on average per reco candidate
    'nhits_mean_max': 20.0,  # pathologically high occupancy
    'fired_mean_min': 2.5,   # minimum layer coverage for a 3-point track fit
    'reco_eff_min':   0.25,  # OMTF reco eff (fraction with omtfPt > 0)
    'charge_agree_min': 0.88,# charge sign agreement (reconstructed vs gen)
    'phib_frac_min':  0.20,  # ≥20% stubs should carry DT bending measurement
    'type_n_min':     2,     # at least 2 different stub types
    'eta_accept_min': 0.80,  # ≥80% of candidates in OMTF |η| 0.82–1.24
}

plt.rcParams.update({
    'figure.dpi': 120, 'axes.labelsize': 10, 'axes.titlesize': 10,
    'legend.fontsize': 8, 'xtick.labelsize': 9, 'ytick.labelsize': 9,
    'axes.grid': True, 'grid.alpha': 0.3,
})


# ── File discovery ───────────────────────────────────────────────────────────
def find_files(ds):
    pattern = f"{EOS_BASE}/{ds}/omtf_hits_{ds}_*.root"
    return sorted(glob.glob(pattern))


def _make_rdf(files, cap=None):
    """Return an RDataFrame over all files, optionally capped at cap entries."""
    rdf = ROOT.RDataFrame(TREE_PATH, ROOT.std.vector('string')(files))
    if cap is not None:
        n = int(rdf.Count().GetValue())
        if n > cap:
            rdf = rdf.Range(cap)
    return rdf


# ── Load scalar branches from files into numpy via RDataFrame.AsNumpy ────────
def load_scalars(files):
    """Return dict of per-entry numpy arrays for all scalar branches."""
    # Full entry count (no cap) first
    rdf_full = ROOT.RDataFrame(TREE_PATH, ROOT.std.vector('string')(files))
    n_entries = int(rdf_full.Count().GetValue())

    rdf = rdf_full
    if n_entries > MAX_ENTRIES:
        rdf = rdf_full.Range(MAX_ENTRIES)

    # Derive hit count from the 'hits' vector branch length
    rdf = rdf.Define('n_hits', 'static_cast<int>(hits.size())')

    # Popcount of omtfFiredLayers bitmask — compute inside RDF (fast C++)
    rdf = rdf.Define('nFiredLayers',
        'int n=0; for(int b=0;b<18;b++) n+=((omtfFiredLayers>>b)&1); return n;')

    # Cast Char_t / Short_t branches to int so AsNumpy doesn't choke on them
    for src, dst in [('muonCharge', 'muonCharge_i'),
                     ('omtfCharge', 'omtfCharge_i'),
                     ('omtfQuality', 'omtfQuality_i'),
                     ('omtfScore', 'omtfScore_i')]:
        rdf = rdf.Define(dst, f'static_cast<int>({src})')
    # killed is Bool_t — cast to int to avoid bool-array edge cases
    rdf = rdf.Define('killed_i', 'static_cast<int>(killed)')

    cols = ['muonPt', 'omtfPt', 'muonPropEta', 'muonDxy', 'muonRho',
            'muonCharge_i', 'omtfCharge_i', 'omtfQuality_i', 'omtfScore_i',
            'killed_i', 'n_hits', 'nFiredLayers']
    np_data = rdf.AsNumpy(cols)

    return {
        'n_entries': n_entries,
        'n_loaded':  len(np_data['muonPt']),
        'muonPt':      np_data['muonPt'].astype(np.float32),
        'omtfPt':      np_data['omtfPt'].astype(np.float32),
        'propEta':     np_data['muonPropEta'].astype(np.float32),
        'muonDxy':     np_data['muonDxy'].astype(np.float32),
        'muonRho':     np_data['muonRho'].astype(np.float32),
        'muonCharge':  np_data['muonCharge_i'].astype(np.int8),
        'omtfCharge':  np_data['omtfCharge_i'].astype(np.int8),
        'omtfQuality': np_data['omtfQuality_i'].astype(np.int8),
        'omtfScore':   np_data['omtfScore_i'].astype(np.float32),
        'firedLayers': np_data['nFiredLayers'].astype(np.int32),
        'nhits':       np_data['n_hits'].astype(np.int32),
        'killed':      np_data['killed_i'].astype(bool),
    }


def load_hit_vectors(files):
    """Return flattened numpy arrays for hit-level branches."""
    # Use a TChain + Draw for vector branches — but only for a hit-level cap.
    # RDataFrame.AsNumpy on vector branches returns arrays of arrays
    # and then we just concatenate.
    cap_entries = min(MAX_ENTRIES, 300_000)  # ~300k entries → manageable
    rdf = _make_rdf(files, cap=cap_entries)
    # Cast signed char / short branches to int so AsNumpy gives numeric RVecs
    rdf = rdf.Define('ht_int',  'ROOT::VecOps::RVec<int>(hits_type.begin(), hits_type.end())')
    rdf = rdf.Define('hp_int',  'ROOT::VecOps::RVec<int>(hits_phiBHw.begin(), hits_phiBHw.end())')
    rdf = rdf.Define('hr_int',  'ROOT::VecOps::RVec<int>(hits_r.begin(), hits_r.end())')
    np_data = rdf.AsNumpy(['ht_int', 'hp_int', 'hr_int'])

    # Each element is a RVec<int>; np.asarray works directly
    htypes = np.concatenate([np.asarray(v, dtype=np.int8)   for v in np_data['ht_int']])
    hphiB  = np.concatenate([np.asarray(v, dtype=np.float32) for v in np_data['hp_int']])
    hr     = np.concatenate([np.asarray(v, dtype=np.float32) for v in np_data['hr_int']])
    return {'type': htypes, 'phiBHw': hphiB, 'r': hr}
    return {'type': htypes, 'phiBHw': hphiB, 'r': hr}


# ── Numerical checks per dataset ─────────────────────────────────────────────
def check_dataset(tag, sc, hv, files):
    lbl, col, ls, (pt_lo, pt_hi), max_dxy = DS_META[tag]
    lines = []

    def p(s):
        lines.append(s)

    def ok_warn_fail(cond_ok, cond_warn, msg_ok, msg_warn, msg_fail):
        if cond_ok:
            p(f"    ✓ {msg_ok}")
            return 'PASS'
        elif cond_warn:
            p(f"    ⚠ {msg_warn}")
            return 'WARN'
        else:
            p(f"    ✗ {msg_fail}")
            return 'FAIL'

    verdicts = {}

    p(f"\n{'='*72}")
    p(f"  DATASET: {tag}  —  {lbl}")
    p(f"{'='*72}")

    # ── 1. Files & event count ──────────────────────────────────────────
    p(f"\n[1] FILES & EVENT COUNT")
    n_files   = len(files)
    n_entries = sc['n_entries']
    expected  = EXPECTED_EVENTS.get(tag, -1)
    p(f"    ROOT files on EOS: {n_files}")
    p(f"    Tree entries (candidates): {n_entries:,}  (loaded: {sc['n_loaded']:,})")
    frac = n_entries / expected if expected > 0 else 1.0
    verdicts['files'] = ok_warn_fail(
        frac >= 0.90, frac >= 0.50,
        f"Event count OK ({100*frac:.0f}% of {expected:,} expected gen events)",
        f"Event count below 90% ({100*frac:.0f}% of {expected:,})",
        f"Event count critically low ({100*frac:.0f}% of {expected:,})")

    # ── 2. Gen pT ──────────────────────────────────────────────────────
    p(f"\n[2] GEN pT SPECTRUM  (expected: flat {pt_lo}–{pt_hi} GeV)")
    pt = sc['muonPt']
    p(f"    min={pt.min():.1f}  max={pt.max():.1f}  mean={pt.mean():.1f}  "
      f"median={np.median(pt):.1f}  std={pt.std():.1f} GeV")
    oob = ((pt < pt_lo) | (pt > pt_hi)).mean()
    verdicts['pt_range'] = ok_warn_fail(
        oob < 0.02, oob < 0.10,
        f"{100*(1-oob):.1f}% of candidates within expected pT range [{pt_lo},{pt_hi}]",
        f"{100*oob:.1f}% outside expected pT range (marginal)",
        f"{100*oob:.1f}% outside expected pT range (critical)")

    # ── 3. OMTF eta acceptance ─────────────────────────────────────────
    p(f"\n[3] OMTF ETA ACCEPTANCE  (|η_prop| in 0.82–1.24)")
    eta = sc['propEta']
    in_acc = ((np.abs(eta) >= 0.82) & (np.abs(eta) <= 1.24)).mean()
    p(f"    η_prop: min={eta.min():.3f}  max={eta.max():.3f}  mean={eta.mean():.3f}")
    p(f"    In OMTF acceptance [0.82,1.24]: {100*in_acc:.1f}%")
    verdicts['eta'] = ok_warn_fail(
        in_acc >= GNN['eta_accept_min'],  in_acc >= 0.60,
        f"Eta acceptance OK ({100*in_acc:.1f}%)",
        f"Eta acceptance marginal ({100*in_acc:.1f}%)",
        f"Eta acceptance too low ({100*in_acc:.1f}%) — check generator config")

    # ── 4. Displacement ────────────────────────────────────────────────
    p(f"\n[4] DISPLACEMENT  (dataset max |dxy| = {max_dxy} cm)")
    dxy = sc['muonDxy']
    p(f"    dxy: min={dxy.min():.2f}  max={dxy.max():.2f}  "
      f"mean={dxy.mean():.4f}  std={dxy.std():.3f} cm")
    if max_dxy == 0:
        prompt = (np.abs(dxy) < 0.5).mean()
        p(f"    Prompt (|dxy|<0.5cm): {100*prompt:.1f}%")
        verdicts['dxy'] = ok_warn_fail(
            prompt >= 0.90, prompt >= 0.75,
            f"Prompt fraction OK ({100*prompt:.1f}%)",
            f"Unexpectedly large displacements ({100*(1-prompt):.1f}% have |dxy|>0.5cm)",
            f"Too many displaced candidates for a prompt dataset")
    else:
        overflow = (np.abs(dxy) > max_dxy * 1.02).mean()
        # flat distribution check: std ≈ max_dxy/sqrt(3)
        expected_std = max_dxy / 3**0.5
        std_ratio = dxy.std() / expected_std if expected_std > 0 else np.nan
        p(f"    |dxy| std={dxy.std():.2f} cm  (expected ~{expected_std:.2f} for flat)")
        p(f"    Overflow (|dxy|>{max_dxy}cm): {100*overflow:.2f}%")
        verdicts['dxy'] = ok_warn_fail(
            overflow < 0.01 and 0.5 < std_ratio < 1.5,
            overflow < 0.05,
            f"Displacement distribution OK (overflow {100*overflow:.2f}%, std-ratio {std_ratio:.2f})",
            f"Displacement marginal (overflow {100*overflow:.2f}%)",
            f"Displacement out of spec (overflow {100*overflow:.2f}%)")

    # ── 5. OMTF reconstruction efficiency ──────────────────────────────
    p(f"\n[5] OMTF RECONSTRUCTION EFFICIENCY")
    omtfPt   = sc['omtfPt']
    matched  = omtfPt > 0
    reco_eff = matched.mean()
    p(f"    Matched (omtfPt>0): {matched.sum():,}/{len(omtfPt):,} = {100*reco_eff:.1f}%")
    if matched.sum() > 0:
        pt_m = omtfPt[matched]
        p(f"    OMTF pT (reco'd): mean={pt_m.mean():.1f}  "
          f"min={pt_m.min():.1f}  max={pt_m.max():.1f} GeV")
    verdicts['reco_eff'] = ok_warn_fail(
        reco_eff >= GNN['reco_eff_min'], reco_eff >= 0.15,
        f"Reco efficiency OK ({100*reco_eff:.1f}%)",
        f"Reco efficiency marginal ({100*reco_eff:.1f}%) — check acceptance",
        f"Reco efficiency critically low ({100*reco_eff:.1f}%)")

    # ── 6. pT resolution ───────────────────────────────────────────────
    p(f"\n[6] pT RESOLUTION  (reco'd, genPt > 2 GeV)")
    valid = matched & (pt > 2.0)
    if valid.sum() > 10:
        res = (omtfPt[valid] - pt[valid]) / pt[valid]
        p(f"    n={valid.sum():,}  mean={res.mean():.3f}  std={res.std():.3f}  "
          f"median={np.median(res):.3f}")
        p10, p90 = np.percentile(res, [10, 90])
        p(f"    [10,90] percentiles: [{p10:.2f}, {p90:.2f}]")
        eff_sigma = (p90 - p10) / 2
        verdicts['pt_res'] = ok_warn_fail(
            eff_sigma < 1.5, eff_sigma < 2.5,
            f"pT resolution reasonable (IQR-based σ={eff_sigma:.2f})",
            f"pT resolution marginal (σ={eff_sigma:.2f})",
            f"pT resolution poor (σ={eff_sigma:.2f}) — possible wrong file content")
    else:
        p(f"    Too few matched entries for resolution")
        verdicts['pt_res'] = 'WARN'

    # ── 7. Charge assignment ────────────────────────────────────────────
    p(f"\n[7] CHARGE ASSIGNMENT")
    m = matched
    if m.sum() > 0:
        agree = (sc['muonCharge'][m] == sc['omtfCharge'][m]).mean()
        p(f"    Agreement: {100*agree:.1f}% (n={m.sum():,})")
        verdicts['charge'] = ok_warn_fail(
            agree >= GNN['charge_agree_min'], agree >= 0.75,
            f"Charge OK ({100*agree:.1f}%)",
            f"Charge marginal ({100*agree:.1f}%)",
            f"Charge poor ({100*agree:.1f}%) — check reconstruction")
    else:
        verdicts['charge'] = 'WARN'

    # ── 8. OMTF Quality & Score ────────────────────────────────────────
    p(f"\n[8] OMTF QUALITY & SCORE")
    qual  = sc['omtfQuality']
    score = sc['omtfScore']
    unique_q, counts_q = np.unique(qual.clip(0, 15), return_counts=True)
    q_dist = {int(v): int(c) for v, c in zip(unique_q, counts_q)}
    p(f"    Quality: mean={qual.mean():.2f}  mode={int(qual.clip(0).flat[np.argmax(np.bincount(qual.clip(0).astype(int)))])}  dist={q_dist}")
    p(f"    Score:   mean={score.mean():.1f}  std={score.std():.1f}  "
      f"min={score.min():.0f}  max={score.max():.0f}")

    # ── 9. Fired layers ────────────────────────────────────────────────
    p(f"\n[9] FIRED LAYERS (from omtfFiredLayers bitmask)")
    fired = sc['firedLayers']
    p(f"    mean={fired.mean():.2f}  std={fired.std():.2f}  "
      f"min={fired.min()}  max={fired.max()}")
    unique_f, counts_f = np.unique(fired, return_counts=True)
    dist_f = {int(v): int(c) for v, c in zip(unique_f, counts_f) if c > 0}
    p(f"    Distribution: {dict(list(dist_f.items())[:12])}")
    verdicts['fired_layers'] = ok_warn_fail(
        fired.mean() >= GNN['fired_mean_min'], fired.mean() >= 1.5,
        f"Fired layer count OK ({fired.mean():.2f} mean)",
        f"Fired layers marginal ({fired.mean():.2f} mean)",
        f"Fired layers critically low ({fired.mean():.2f} mean)")

    # ── 10. Hit multiplicity (GNN graph size) ──────────────────────────
    p(f"\n[10] HIT MULTIPLICITY — GNN GRAPH SIZE")
    nhits_all = sc['nhits']
    nhits_reco = nhits_all[matched]
    p(f"    All entries:  mean={nhits_all.mean():.2f}  std={nhits_all.std():.2f}  "
      f"min={nhits_all.min()}  max={nhits_all.max()}")
    if len(nhits_reco) > 0:
        p(f"    Reco'd only:  mean={nhits_reco.mean():.2f}  std={nhits_reco.std():.2f}  "
          f"min={nhits_reco.min()}  max={nhits_reco.max()}")
    p(f"    nhits=0: {(nhits_all==0).sum():,} ({100*(nhits_all==0).mean():.1f}%) — unmatched gen muons")
    nhits_pos = nhits_all[nhits_all > 0]
    if len(nhits_pos) > 0:
        p(f"    nhits>0: mean={nhits_pos.mean():.2f}  percentiles [5,50,95]% = "
          f"{np.percentile(nhits_pos,[5,50,95]).round(1).tolist()}")
    verdicts['nhits'] = ok_warn_fail(
        GNN['nhits_mean_min'] <= nhits_pos.mean() <= GNN['nhits_mean_max'] if len(nhits_pos) > 0 else False,
        len(nhits_pos) > 0 and nhits_pos.mean() >= 2.0,
        f"Hit count in GNN-tractable range ({nhits_pos.mean():.2f} mean for reco'd)",
        f"Hit count marginal ({nhits_pos.mean():.2f} mean)",
        f"Hit count out of expected range ({nhits_pos.mean():.2f} mean)")

    # ── 11. Hit detector types ─────────────────────────────────────────
    p(f"\n[11] HIT DETECTOR TYPES  (GNN node feature diversity)")
    htypes = hv['type']
    hphiB  = hv['phiBHw']
    hr     = hv['r']
    n_hits_total = len(htypes)
    p(f"    Total hits analysed: {n_hits_total:,}")
    if n_hits_total > 0:
        type_counts = {}
        for t, name in HIT_TYPE_NAMES.items():
            cnt = (htypes == t).sum()
            type_counts[name] = cnt
            p(f"    {name:10s} (type {t}): {cnt:7,}  ({100*cnt/n_hits_total:.1f}%)")
        unknown = np.isin(htypes, list(HIT_TYPE_NAMES.keys()), invert=True).sum()
        if unknown > 0:
            p(f"    Unknown type: {unknown:7,}  ({100*unknown/n_hits_total:.1f}%)")

        # phiB (DT bending) stub fraction
        phib_frac = (np.abs(hphiB) > 0).mean()
        p(f"    PhiB measurement (|phiBHw|>0): {100*phib_frac:.1f}% of all hits")

        # Radial distribution
        p(f"    Hits_r: min={hr.min():.0f}  max={hr.max():.0f}  mean={hr.mean():.0f} cm")

        n_types = sum(1 for t, n in type_counts.items() if n > 0)
        verdicts['hit_types'] = ok_warn_fail(
            n_types >= GNN['type_n_min'] and phib_frac >= GNN['phib_frac_min'],
            n_types >= 1 and phib_frac >= 0.05,
            f"Hit type diversity OK ({n_types} types, phiB={100*phib_frac:.1f}%)",
            f"Hit type diversity marginal ({n_types} types, phiB={100*phib_frac:.1f}%)",
            f"Hit type diversity insufficient — GNN edge features will be degraded")
    else:
        verdicts['hit_types'] = 'FAIL'

    # ── GNN readiness summary ──────────────────────────────────────────
    p(f"\n{'─'*72}")
    p(f"  GNN READINESS VERDICT — {tag}")
    p(f"{'─'*72}")
    all_pass  = all(v == 'PASS' for v in verdicts.values())
    any_fail  = any(v == 'FAIL' for v in verdicts.values())
    for k, v in verdicts.items():
        sym = '✓' if v == 'PASS' else ('✗' if v == 'FAIL' else '~')
        p(f"    {sym} {k:<20s} {v}")
    if all_pass:
        overall = 'GO'
        p(f"\n  ► OVERALL: GO — ready for model design")
    elif any_fail:
        overall = 'NO-GO'
        p(f"\n  ► OVERALL: NO-GO — fix failing checks before model design")
    else:
        overall = 'MARGINAL'
        p(f"\n  ► OVERALL: MARGINAL — proceed with caution")
    p(f"{'─'*72}")

    return lines, verdicts, overall


# ── Per-dataset plots ────────────────────────────────────────────────────────
def plot_dataset(tag, sc, hv):
    lbl, col, ls, (pt_lo, pt_hi), max_dxy = DS_META[tag]
    matched = sc['omtfPt'] > 0
    pt      = sc['muonPt']
    nhits   = sc['nhits']
    nhits_pos = nhits[nhits > 0]

    fig, axes = plt.subplots(3, 3, figsize=(15, 11))
    ax = axes.flatten()
    fig.suptitle(f'Dataset {tag}: {lbl}  (n={sc["n_entries"]:,} entries, '
                 f'{sc["n_loaded"]:,} loaded)', fontsize=12, fontweight='bold')

    # 1. Gen pT
    ax[0].hist(pt, bins=50, color=col, alpha=0.7, edgecolor='black', lw=0.4)
    ax[0].axvline(pt_lo, color='red', lw=1.2, ls='--', label=f'range [{pt_lo},{pt_hi}]')
    ax[0].axvline(pt_hi, color='red', lw=1.2, ls='--')
    ax[0].set_xlabel('Gen pT [GeV]'); ax[0].set_title('Gen pT spectrum')
    ax[0].legend(fontsize=8)

    # 2. Gen eta propagated
    ax[1].hist(sc['propEta'], bins=50, range=(-1.5, 1.5), color=col, alpha=0.7,
               edgecolor='black', lw=0.4)
    for xv in [0.82, 1.24, -0.82, -1.24]:
        ax[1].axvline(xv, color='red', lw=1, ls='--')
    ax[1].set_xlabel('η propagated'); ax[1].set_title('Eta (OMTF acceptance dashed)')

    # 3. dxy
    ax[2].hist(sc['muonDxy'], bins=60, color=col, alpha=0.7, edgecolor='black', lw=0.4)
    if max_dxy > 0:
        ax[2].axvline(max_dxy, color='red', lw=1.2, ls='--')
        ax[2].axvline(-max_dxy, color='red', lw=1.2, ls='--')
    ax[2].set_xlabel('dxy [cm]'); ax[2].set_title('Gen dxy displacement')

    # 4. Hit multiplicity
    ax[3].hist(nhits_pos, bins=np.arange(-0.5, 22),
               color=col, alpha=0.7, edgecolor='black', lw=0.4)
    ax[3].axvline(nhits_pos.mean(), color='black', ls=':', lw=1.5,
                  label=f'mean={nhits_pos.mean():.1f}')
    ax[3].set_xlabel('Hits per reco candidate'); ax[3].set_title('Hit multiplicity (nhits > 0)')
    ax[3].legend(fontsize=8)

    # 5. Fired layers
    ax[4].hist(sc['firedLayers'], bins=np.arange(-0.5, 19),
               color=col, alpha=0.7, edgecolor='black', lw=0.4)
    ax[4].axvline(sc['firedLayers'].mean(), color='black', ls=':', lw=1.5,
                  label=f"mean={sc['firedLayers'].mean():.1f}")
    ax[4].set_xlabel('Fired layers'); ax[4].set_title('Fired OMTF layers (bitmask popcount)')
    ax[4].legend(fontsize=8)

    # 6. Detector type bar
    if len(hv['type']) > 0:
        htypes = hv['type']
        names  = [HIT_TYPE_NAMES.get(t, f'type{t}') for t in sorted(HIT_TYPE_NAMES.keys())]
        counts = [int((htypes == t).sum()) for t in sorted(HIT_TYPE_NAMES.keys())]
        bars   = ax[5].bar(names, counts, color=[col, 'gray', 'lightblue'], alpha=0.8,
                           edgecolor='black', lw=0.5)
        ax[5].set_ylabel('Hit count'); ax[5].set_title('Hit detector type distribution')
        for bar, cnt in zip(bars, counts):
            ax[5].text(bar.get_x() + bar.get_width()/2, bar.get_height() * 1.01,
                       f'{cnt//1000}k', ha='center', va='bottom', fontsize=8)

    # 7. OMTF pT vs Gen pT
    if matched.sum() > 0:
        mx = max(pt.max(), sc['omtfPt'].max()) * 1.05
        ax[6].hexbin(pt[matched], sc['omtfPt'][matched],
                     gridsize=30, cmap='plasma', mincnt=1,
                     extent=[0, min(mx, 250), 0, min(mx, 250)])
        ax[6].plot([0, min(mx, 250)], [0, min(mx, 250)], 'w--', lw=1)
        ax[6].set_xlabel('Gen pT [GeV]'); ax[6].set_ylabel('OMTF pT [GeV]')
        ax[6].set_title(f'OMTF pT vs Gen pT (n={matched.sum():,})')

    # 8. pT resolution
    valid = matched & (pt > 2.0)
    if valid.sum() > 5:
        res = (sc['omtfPt'][valid] - pt[valid]) / pt[valid]
        ax[7].hist(res, bins=60, range=(-2, 6), color=col, alpha=0.7, edgecolor='black', lw=0.4)
        ax[7].axvline(0, color='red', lw=1, ls='--')
        ax[7].axvline(np.median(res), color='black', ls=':', lw=1.5,
                      label=f'median={np.median(res):.2f}')
        ax[7].legend(fontsize=8)
    ax[7].set_xlabel('(omtfPt - genPt) / genPt'); ax[7].set_title('pT resolution')

    # 9. phiBHw distribution (DT bending)
    if len(hv['phiBHw']) > 0:
        phiB = hv['phiBHw']
        ax[8].hist(phiB, bins=80, range=(-1100, 1100),
                   color=col, alpha=0.7, edgecolor='black', lw=0.4)
        ax[8].axvline(0, color='red', lw=1, ls='--')
        phib_frac = (np.abs(phiB) > 0).mean()
        ax[8].set_xlabel('phiBHw [a.u.]')
        ax[8].set_title(f'DT bending measurement ({100*phib_frac:.1f}% of stubs)')

    plt.tight_layout()
    out = os.path.join(OUT_DIR, f'prod_{tag}.pdf')
    plt.savefig(out, bbox_inches='tight')
    plt.close()
    print(f"  → {out}")


# ── Comparison plots ─────────────────────────────────────────────────────────
def plot_pu_comparison(all_sc):
    pairs = [(s, b) for s, b in [('S1','B1'),('S2','B2'),('S3','B3')]
             if s in all_sc and b in all_sc]
    if not pairs:
        return
    fig, axes = plt.subplots(len(pairs), 4, figsize=(18, 4.5*len(pairs)))
    if len(pairs) == 1:
        axes = [axes]
    for row, (noPU, PU) in enumerate(pairs):
        for ax, (var, label, bins) in zip(axes[row], [
            ('nhits',       'N stubs (nhits>0)',    np.arange(-0.5, 20)),
            ('firedLayers', 'Fired layers',          np.arange(-0.5, 19)),
            ('omtfScore',   'OMTF score',            50),
            ('omtfQuality', 'OMTF quality',          np.arange(-0.5, 17)),
        ]):
            for tag in [noPU, PU]:
                sc = all_sc[tag]
                lbl, c, lsty, *_ = DS_META[tag]
                d = sc[var].astype(float)
                if var == 'nhits':
                    d = d[d > 0]
                ax.hist(d, bins=bins, histtype='step', label=f'{tag} ({lbl.split(" ")[0]})',
                        color=c, linestyle=lsty, linewidth=1.8, density=True)
            ax.set_xlabel(label); ax.set_ylabel('Density (normalised)')
            ax.set_title(f'{noPU} vs {PU}: {label}')
            ax.legend(fontsize=8)
    plt.suptitle('No-PU vs PU200 comparison', fontsize=13, fontweight='bold')
    plt.tight_layout()
    out = os.path.join(OUT_DIR, 'prod_comparison_PU.pdf')
    plt.savefig(out, bbox_inches='tight'); plt.close()
    print(f"  → {out}")


def plot_turnon(all_sc):
    fig, ax = plt.subplots(figsize=(11, 5))
    pt_edges = np.array([0, 5, 10, 15, 18, 22, 26, 30, 40, 55, 75, 110, 160, 210])
    pt_centers = 0.5 * (pt_edges[:-1] + pt_edges[1:])
    for tag, lbl, col, ls, pt_r, _ in DATASETS:
        if tag not in all_sc:
            continue
        sc = all_sc[tag]
        effs = []
        for lo, hi in zip(pt_edges[:-1], pt_edges[1:]):
            sel  = (sc['muonPt'] >= lo) & (sc['muonPt'] < hi)
            ntot = sel.sum()
            npass = ((sc['omtfPt'] >= 22) & sel).sum()
            effs.append(npass / ntot if ntot > 0 else np.nan)
        effs = np.array(effs)
        ok   = ~np.isnan(effs)
        ax.plot(pt_centers[ok], effs[ok], label=lbl, color=col, linestyle=ls,
                linewidth=1.8, marker='o', markersize=3)
    ax.axvline(22, color='gray', ls=':', label='22 GeV threshold')
    ax.axhline(1.0, color='lightgray', lw=0.8)
    ax.set(xlabel='Gen pT [GeV]', ylabel='ε (omtfPt ≥ 22 GeV)',
           title='Trigger turn-on curves', ylim=(-0.05, 1.15))
    ax.legend(ncol=2, fontsize=8)
    out = os.path.join(OUT_DIR, 'prod_comparison_turnon.pdf')
    plt.savefig(out, bbox_inches='tight'); plt.close()
    print(f"  → {out}")


def plot_gnn_readiness(all_sc, all_hv):
    loaded = [t for t, *_ in DATASETS if t in all_sc]
    fig, axes = plt.subplots(1, 3, figsize=(17, 5))

    # A: nhits distribution overlay
    ax = axes[0]
    for tag in loaded:
        lbl, col, ls, *_ = DS_META[tag]
        nhits = all_sc[tag]['nhits']
        nhits = nhits[nhits > 0]
        ax.hist(nhits, bins=np.arange(-0.5, 22), histtype='step',
                label=f'{tag} (μ={nhits.mean():.1f})', color=col, linestyle=ls,
                linewidth=1.5, density=True)
    ax.axvline(GNN['nhits_mean_min'], color='red', ls=':', lw=1.2, label='GNN min')
    ax.set(xlabel='Hits per reco candidate (nhits>0)', ylabel='Density',
           title='Hit multiplicity — all datasets')
    ax.legend(ncol=2, fontsize=7)

    # B: Fired layers overlay
    ax = axes[1]
    for tag in loaded:
        lbl, col, ls, *_ = DS_META[tag]
        fired = all_sc[tag]['firedLayers']
        ax.hist(fired, bins=np.arange(-0.5, 19), histtype='step',
                label=f'{tag} (μ={fired.mean():.1f})', color=col, linestyle=ls,
                linewidth=1.5, density=True)
    ax.axvline(GNN['fired_mean_min'], color='red', ls=':', lw=1.2, label='GNN min')
    ax.set(xlabel='Fired layers', ylabel='Density',
           title='Fired layer count — all datasets')
    ax.legend(ncol=2, fontsize=7)

    # C: Mean nhits vs dataset (bar chart)
    ax = axes[2]
    means   = [all_sc[t]['nhits'][all_sc[t]['nhits'] > 0].mean() for t in loaded]
    errs    = [all_sc[t]['nhits'][all_sc[t]['nhits'] > 0].std()  for t in loaded]
    colors  = [DS_META[t][1] for t in loaded]
    bars    = ax.bar(loaded, means, yerr=errs, capsize=3, color=colors, alpha=0.8,
                     edgecolor='black', lw=0.5)
    ax.axhline(GNN['nhits_mean_min'], color='red', ls='--', lw=1.2, label='GNN min threshold')
    ax.axhline(GNN['nhits_mean_max'], color='orange', ls='--', lw=1.2, label='GNN max threshold')
    ax.set(ylabel='Mean nhits (reco\'d)', title='Mean hit multiplicity per dataset')
    ax.legend(fontsize=8)

    plt.suptitle('GNN Readiness — Stub Occupancy', fontsize=12, fontweight='bold')
    plt.tight_layout()
    out = os.path.join(OUT_DIR, 'prod_comparison_gnn.pdf')
    plt.savefig(out, bbox_inches='tight'); plt.close()
    print(f"  → {out}")


# ── Summary table ────────────────────────────────────────────────────────────
def make_summary(all_sc, all_verdicts, all_overall):
    lines = []
    lines.append('\n' + '='*95)
    lines.append('PRODUCTION DATASET VALIDATION SUMMARY')
    lines.append('='*95)
    hdr = (f"{'DS':<5} {'N_entries':>10} {'GenPt_mean':>10} {'dxy_std':>8} {'RecoEff':>8} "
           f"{'nhits_mean':>10} {'FiredL':>8} {'ChargeAgr':>10} {'Overall':>10}")
    lines.append(hdr)
    lines.append('-' * len(hdr))
    for tag, *_ in DATASETS:
        if tag not in all_sc:
            lines.append(f"{tag:<5} MISSING")
            continue
        sc  = all_sc[tag]
        ov  = all_overall.get(tag, '?')
        m   = sc['omtfPt'] > 0
        nhp = sc['nhits'][sc['nhits'] > 0]
        chg = (sc['muonCharge'][m] == sc['omtfCharge'][m]).mean() if m.sum() > 0 else float('nan')
        lines.append(
            f"{tag:<5} {sc['n_entries']:>10,} {sc['muonPt'].mean():>10.1f} "
            f"{sc['muonDxy'].std():>8.2f} {100*m.mean():>7.1f}% "
            f"{nhp.mean() if len(nhp)>0 else 0:>10.2f} "
            f"{sc['firedLayers'].mean():>8.2f} {100*chg:>9.1f}% "
            f"{ov:>10}"
        )
    lines.append('='*95)
    return lines


# ── Main ─────────────────────────────────────────────────────────────────────
def main():
    print(f"\nOMTF HECIN Production Validation")
    print(f"EOS base: {EOS_BASE}")
    print(f"Output:   {OUT_DIR}\n")

    all_sc       = {}
    all_hv       = {}
    all_verdicts = {}
    all_overall  = {}
    all_report   = []

    for tag, *_ in DATASETS:
        files = find_files(tag)
        print(f"{'─'*50}")
        print(f"{tag}: {len(files)} files found")
        if not files:
            print(f"  SKIP — no files in {EOS_BASE}/{tag}/")
            continue

        print(f"  Loading scalars...", end=' ', flush=True)
        sc = load_scalars(files)
        print(f"{sc['n_entries']:,} entries, {sc['n_loaded']:,} loaded")

        print(f"  Loading hit vectors...", end=' ', flush=True)
        hv = load_hit_vectors(files)
        print(f"{len(hv['type']):,} hits")

        all_sc[tag] = sc
        all_hv[tag] = hv

        print(f"  Running checks...", flush=True)
        report_lines, verdicts, overall = check_dataset(tag, sc, hv, files)
        all_verdicts[tag] = verdicts
        all_overall[tag]  = overall
        all_report.extend(report_lines)
        for l in report_lines:
            print(l)

        # Per-dataset report file
        rpt_path = os.path.join(OUT_DIR, f'prod_report_{tag}.txt')
        with open(rpt_path, 'w') as f:
            f.write('\n'.join(report_lines))
        print(f"\n  → {rpt_path}")

        # Per-dataset plot
        print(f"  Plotting {tag}...", end=' ', flush=True)
        plot_dataset(tag, sc, hv)

    # Comparison plots
    if len(all_sc) > 1:
        print(f"\n{'─'*50}")
        print("Generating comparison plots...")
        plot_pu_comparison(all_sc)
        plot_turnon(all_sc)
        plot_gnn_readiness(all_sc, all_hv)

    # Final summary
    summary = make_summary(all_sc, all_verdicts, all_overall)
    all_report.extend(summary)
    for l in summary:
        print(l)

    # Write combined report
    full_report = os.path.join(OUT_DIR, 'PRODUCTION_VALIDATION.txt')
    with open(full_report, 'w') as f:
        f.write('\n'.join(all_report))
    print(f"\nFull report: {full_report}")


if __name__ == '__main__':
    main()
