#!/usr/bin/env python3
"""
scripts/audit/audit_gen_particles.py

Generator-level audit for GMT-visible-stub G-campaign datasets.

Usage:
    python3 audit_gen_particles.py --files omtf_nano_G1_*.root --dataset G1
    python3 audit_gen_particles.py --dir /eos/user/p/pleguina/omtf_hecin_datasets/prod/G2/ --dataset G2
    python3 audit_gen_particles.py --all  # audit all G datasets found in EOS

Requires: uproot, numpy, awkward (available in CMSSW Python3 environment or
          standalone: pip install uproot awkward numpy)

Checks performed per dataset:
  - nGenMuon multiplicity (mode and 100% fraction)
    - GenMuon_eta in OMTF overlap [0.82, 1.24] (G1-G6), barrel |eta|<0.75 (G7-G8),
        or high-eta hard-negative band 1.30<|eta|<1.80 (G9/G10 split)
  - GenMuon_pt range plausibility
  - GenMuon_charge balance (for single-muon datasets)
  - GenMuon_dXY range (for displaced datasets G3-G5)

Exits with code 0 if all checks pass, 1 if any check fails.
"""

import argparse
import sys
import os
import glob

try:
    import uproot
    import numpy as np
    import awkward as ak
except ImportError:
    sys.exit(
        "ERROR: uproot/awkward/numpy not available.\n"
        "  cmsenv and: pip install uproot awkward numpy\n"
        "  or run inside the CMSSW Python3 virtualenv."
    )

# ── Dataset specs ─────────────────────────────────────────────────────────────

DATASET_SPECS = {
    "G1": dict(
        expected_n_gen=1,
        eta_mode="overlap",     # 0.82 <= |eta| <= 1.24
        eta_pass_threshold=0.98,
        pt_min=2.0, pt_max=200.0,
        dxy_mode="prompt",      # dxy ~ 0
        label="G1_singlePromptOverlap",
    ),
    "G2": dict(
        expected_n_gen=1,
        eta_mode="overlap",
        eta_pass_threshold=0.98,
        pt_min=2.0, pt_max=200.0,
        dxy_mode="prompt",
        label="G2_singlePromptOverlap_PU200",
    ),
    "G3": dict(
        expected_n_gen=1,
        eta_mode="overlap",
        eta_pass_threshold=0.98,
        pt_min=2.0, pt_max=200.0,
        dxy_mode="displaced_0_50",  # dxy in [0, 50] cm
        label="G3_singleDisplacedOverlap",
    ),
    "G4": dict(
        expected_n_gen=1,
        eta_mode="overlap",
        eta_pass_threshold=0.98,
        pt_min=2.0, pt_max=200.0,
        dxy_mode="displaced_0_50",
        label="G4_singleDisplacedOverlap_PU200",
    ),
    "G5": dict(
        expected_n_gen=2,
        eta_mode="overlap",
        eta_pass_threshold=0.98,
        pt_min=2.0, pt_max=200.0,
        dxy_mode="displaced_0_30",  # dxy in [0, 30] cm
        label="G5_twoDisplacedOverlap_PU200",
    ),
    "G6": dict(
        expected_n_gen=3,
        eta_mode="overlap",
        eta_pass_threshold=0.98,
        pt_min=5.0, pt_max=80.0,
        dxy_mode="prompt",
        label="G6_triPromptOverlap_PU200",
    ),
    "G7": dict(
        expected_n_gen=1,
        eta_mode="barrel",      # |eta| < 0.75
        eta_pass_threshold=0.98,
        pt_min=2.0, pt_max=200.0,
        dxy_mode="prompt",
        label="G7_hardNegLowEtaMuon",
    ),
    "G8": dict(
        expected_n_gen=1,
        eta_mode="barrel",
        eta_pass_threshold=0.98,
        pt_min=2.0, pt_max=200.0,
        dxy_mode="prompt",
        label="G8_hardNegLowEtaMuon_PU200",
    ),
    "G9_pos": dict(
        expected_n_gen=1,
        eta_mode="high_eta_hardneg",
        eta_pass_threshold=0.98,
        pt_min=2.0, pt_max=200.0,
        dxy_mode="prompt",
        label="G9_pos_hardNegHighEtaMuon",
    ),
    "G9_neg": dict(
        expected_n_gen=1,
        eta_mode="high_eta_hardneg",
        eta_pass_threshold=0.98,
        pt_min=2.0, pt_max=200.0,
        dxy_mode="prompt",
        label="G9_neg_hardNegHighEtaMuon",
    ),
    "G10_pos": dict(
        expected_n_gen=1,
        eta_mode="high_eta_hardneg",
        eta_pass_threshold=0.98,
        pt_min=2.0, pt_max=200.0,
        dxy_mode="prompt",
        label="G10_pos_hardNegHighEtaMuon_PU200",
    ),
    "G10_neg": dict(
        expected_n_gen=1,
        eta_mode="high_eta_hardneg",
        eta_pass_threshold=0.98,
        pt_min=2.0, pt_max=200.0,
        dxy_mode="prompt",
        label="G10_neg_hardNegHighEtaMuon_PU200",
    ),
}

ETA_OVERLAP_MIN = 0.82
ETA_OVERLAP_MAX = 1.24
ETA_BARREL_MAX  = 0.75
ETA_HARDNEG_ENDCAP_MIN = 1.30
ETA_HARDNEG_ENDCAP_MAX = 1.80

# ── Helpers ───────────────────────────────────────────────────────────────────

def load_nano_branches(files):
    """Load generator branches from a list of NanoAOD-style ROOT files."""
    branches = [
        "nGenMuon",
        "GenMuon_pt",
        "GenMuon_eta",
        "GenMuon_phi",
        "GenMuon_charge",
        "GenMuon_pdgId",
        "GenMuon_status",
        "GenMuon_dxy",
    ]
    arrays = {}
    for b in branches:
        arrays[b] = []

    n_files_loaded = 0
    for f in files:
        try:
            with uproot.open(f) as fh:
                # Try NanoAOD tree name first, then OMTF custom tree
                tree = None
                for tname in ("Events", "omtfNano", "omtf"):
                    if tname in fh:
                        tree = fh[tname]
                        break
                if tree is None:
                    print(f"  WARNING: no recognised tree in {f}, skipping")
                    continue
                avail = set(tree.keys())
                for b in branches:
                    if b in avail:
                        arrays[b].append(tree[b].array(library="ak"))
            n_files_loaded += 1
        except Exception as e:
            print(f"  WARNING: failed to read {f}: {e}")

    if n_files_loaded == 0:
        return None, 0

    merged = {}
    for b in branches:
        if arrays[b]:
            merged[b] = ak.concatenate(arrays[b], axis=0)
        else:
            merged[b] = None

    return merged, n_files_loaded


def check_eta_overlap(eta_flat):
    """Return fraction of muons with 0.82 <= |eta| <= 1.24."""
    abseta = np.abs(np.asarray(eta_flat))
    mask = (abseta >= ETA_OVERLAP_MIN) & (abseta <= ETA_OVERLAP_MAX)
    return float(np.mean(mask))


def check_eta_barrel(eta_flat):
    """Return fraction of muons with |eta| < 0.75 (barrel, outside OMTF overlap)."""
    abseta = np.abs(np.asarray(eta_flat))
    mask = abseta < ETA_BARREL_MAX
    return float(np.mean(mask))


def check_eta_high_hardneg_endcap(eta_flat):
    """Return fraction of muons with 1.30 <= |eta| <= 1.80."""
    abseta = np.abs(np.asarray(eta_flat))
    mask = (abseta >= ETA_HARDNEG_ENDCAP_MIN) & (abseta <= ETA_HARDNEG_ENDCAP_MAX)
    return float(np.mean(mask))


def check_dxy(dxy_flat, mode):
    """Return (min, max, pass_fraction) for the expected dxy window."""
    dxy = np.abs(np.asarray(dxy_flat))
    if mode == "prompt":
        # Allow small smearing: 95% of events should have |dxy| < 1 cm
        frac = float(np.mean(dxy < 1.0))
        return float(dxy.min()), float(dxy.max()), frac, "< 1 cm"
    elif mode == "displaced_0_50":
        frac = float(np.mean((dxy >= 0.0) & (dxy <= 52.0)))  # 52 cm = 50 + 4% margin
        return float(dxy.min()), float(dxy.max()), frac, "0–50 cm"
    elif mode == "displaced_0_30":
        frac = float(np.mean((dxy >= 0.0) & (dxy <= 32.0)))
        return float(dxy.min()), float(dxy.max()), frac, "0–30 cm"
    return 0.0, 0.0, 0.0, "unknown"


# ── Per-dataset audit ─────────────────────────────────────────────────────────

def audit_dataset(dataset_tag, files):
    spec = DATASET_SPECS.get(dataset_tag)
    if spec is None:
        print(f"[{dataset_tag}] ERROR: unknown dataset tag")
        return False

    print(f"\n{'='*70}")
    print(f"  Dataset: {spec['label']}")
    print(f"  Files:   {len(files)}")
    print(f"{'='*70}")

    data, n_loaded = load_nano_branches(files)
    if data is None or n_loaded == 0:
        print(f"  FATAL: could not load any files for {dataset_tag}")
        return False

    print(f"  Files loaded: {n_loaded}")
    n_events = int(ak.num(data["nGenMuon"], axis=0)) if data["nGenMuon"] is not None else 0
    print(f"  Events:  {n_events}")

    all_pass = True

    # ── 1. nGenMuon multiplicity ─────────────────────────────────────────────
    if data["nGenMuon"] is not None:
        n_gen = np.asarray(data["nGenMuon"])
        expected = spec["expected_n_gen"]
        frac_correct = float(np.mean(n_gen == expected))
        mode_val = int(np.bincount(n_gen).argmax()) if len(n_gen) > 0 else -1
        status = "PASS" if frac_correct >= 0.999 else "FAIL"
        if status == "FAIL":
            all_pass = False
        print(f"  nGenMuon == {expected}: {frac_correct*100:.1f}%  (mode={mode_val})  [{status}]")
        if status == "FAIL":
            unique, counts = np.unique(n_gen, return_counts=True)
            for u, c in zip(unique, counts):
                print(f"    nGenMuon={u}: {c} events ({100*c/len(n_gen):.1f}%)")
    else:
        print("  nGenMuon: branch not found  [SKIP]")

    # ── 2. Eta domain ─────────────────────────────────────────────────────────
    if data["GenMuon_eta"] is not None:
        eta_flat = ak.to_numpy(ak.flatten(data["GenMuon_eta"]))
        eta_mode = spec["eta_mode"]
        thresh = spec["eta_pass_threshold"]

        if eta_mode == "overlap":
            frac = check_eta_overlap(eta_flat)
            status = "PASS" if frac >= thresh else "FAIL"
            if status == "FAIL":
                all_pass = False
            print(f"  eta in overlap [0.82,1.24]: {frac*100:.1f}%  [{status}]")
            # Also check that overlap-in fraction is near zero (should not have barrel muons)
            frac_barrel = check_eta_barrel(eta_flat)
            if frac_barrel > 0.02:
                print(f"  WARNING: {frac_barrel*100:.1f}% of muons have |eta| < 0.75 (unexpected for overlap sample)")

        elif eta_mode == "barrel":
            frac = check_eta_barrel(eta_flat)
            status = "PASS" if frac >= thresh else "FAIL"
            if status == "FAIL":
                all_pass = False
            print(f"  eta in barrel [|eta|<0.75]: {frac*100:.1f}%  [{status}]")
            # For hard-negative, check overlap contamination is near zero
            frac_overlap = check_eta_overlap(eta_flat)
            if frac_overlap > 0.01:
                print(f"  WARNING: {frac_overlap*100:.1f}% of muons in OMTF overlap — expected ~0% for hard-negative")
                all_pass = False
            else:
                print(f"  eta overlap contamination: {frac_overlap*100:.2f}%  [PASS]")

        elif eta_mode == "high_eta_hardneg":
            frac = check_eta_high_hardneg_endcap(eta_flat)
            status = "PASS" if frac >= thresh else "FAIL"
            if status == "FAIL":
                all_pass = False
            print(f"  eta in high-eta hard-negative [1.30,1.80]: {frac*100:.1f}%  [{status}]")
            frac_overlap = check_eta_overlap(eta_flat)
            if frac_overlap > 0.01:
                print(f"  WARNING: {frac_overlap*100:.1f}% of muons in OMTF overlap - expected ~0% for high-eta hard-negative")
                all_pass = False
            else:
                print(f"  eta overlap contamination: {frac_overlap*100:.2f}%  [PASS]")

        abseta = np.abs(eta_flat)
        print(f"  eta range: [{abseta.min():.3f}, {abseta.max():.3f}]")
    else:
        print("  GenMuon_eta: branch not found  [SKIP]")

    # ── 3. pT range ───────────────────────────────────────────────────────────
    if data["GenMuon_pt"] is not None:
        pt_flat = ak.to_numpy(ak.flatten(data["GenMuon_pt"]))
        pt_min_obs = float(pt_flat.min())
        pt_max_obs = float(pt_flat.max())
        pt_min_exp = spec["pt_min"]
        pt_max_exp = spec["pt_max"]
        pt_ok = (pt_min_obs >= pt_min_exp * 0.8) and (pt_max_obs <= pt_max_exp * 1.2)
        status = "PASS" if pt_ok else "WARN"
        print(f"  pT range: [{pt_min_obs:.1f}, {pt_max_obs:.1f}] GeV  (expected [{pt_min_exp}, {pt_max_exp}])  [{status}]")
    else:
        print("  GenMuon_pt: branch not found  [SKIP]")

    # ── 4. Charge balance ─────────────────────────────────────────────────────
    if data["GenMuon_charge"] is not None and spec["expected_n_gen"] == 1:
        charge_flat = ak.to_numpy(ak.flatten(data["GenMuon_charge"]))
        n_plus  = int(np.sum(charge_flat > 0))
        n_minus = int(np.sum(charge_flat < 0))
        total   = n_plus + n_minus
        balance = min(n_plus, n_minus) / max(n_plus, n_minus, 1)
        status = "PASS" if balance >= 0.45 else "WARN"
        print(f"  Charge: mu+={n_plus} ({100*n_plus/total:.1f}%), mu-={n_minus} ({100*n_minus/total:.1f}%)  balance={balance:.3f}  [{status}]")

    # ── 5. dxy (displaced samples) ────────────────────────────────────────────
    if data["GenMuon_dxy"] is not None:
        dxy_flat = ak.to_numpy(ak.flatten(data["GenMuon_dxy"]))
        dxy_min, dxy_max, dxy_frac, window_label = check_dxy(dxy_flat, spec["dxy_mode"])
        status = "PASS" if dxy_frac >= 0.95 else "FAIL"
        if status == "FAIL":
            all_pass = False
        print(f"  dxy range: [{dxy_min:.2f}, {dxy_max:.2f}] cm  ({window_label}: {dxy_frac*100:.1f}%)  [{status}]")
    else:
        if spec["dxy_mode"] != "prompt":
            print("  GenMuon_dxy: branch not found (expected for displaced sample)  [WARN]")

    # ── Summary ───────────────────────────────────────────────────────────────
    overall = "PASS" if all_pass else "FAIL"
    print(f"\n  Overall: [{overall}]\n")
    return all_pass


# ── Main ──────────────────────────────────────────────────────────────────────

def parse_args():
    p = argparse.ArgumentParser(description="Generator-level audit for G-campaign datasets")
    p.add_argument("--dataset", "-d", help="Dataset tag, e.g. G1, G2, ..., G10_pos")
    p.add_argument("--files", "-f", nargs="*", help="List of ROOT NanoAOD files to audit")
    p.add_argument("--dir", help="Directory containing omtf_nano_<DATASET>_*.root files")
    p.add_argument("--eos-base", default="/eos/user/p/pleguina/omtf_hecin_datasets/prod",
                   help="EOS base directory for --all mode")
    p.add_argument("--all", action="store_true", help="Audit all G datasets found under --eos-base")
    p.add_argument("--max-files", type=int, default=50,
                   help="Maximum number of files to load per dataset (default 50)")
    return p.parse_args()


def main():
    args = parse_args()

    if args.all:
        results = {}
        for tag in sorted(DATASET_SPECS.keys()):
            pattern = os.path.join(args.eos_base, f"omtf_nano_{tag}_*.root")
            files = sorted(glob.glob(pattern))[: args.max_files]
            if not files:
                print(f"[{tag}] No files found at {pattern}, skipping")
                results[tag] = None
                continue
            results[tag] = audit_dataset(tag, files)

        print("\n" + "=" * 70)
        print("  SUMMARY")
        print("=" * 70)
        for tag, ok in results.items():
            if ok is None:
                status = "SKIP (no files)"
            elif ok:
                status = "PASS"
            else:
                status = "FAIL"
            print(f"  {tag:<6}  {status}")
        print()

        failed = [tag for tag, ok in results.items() if ok is False]
        if failed:
            print(f"  FAILED datasets: {', '.join(failed)}")
            sys.exit(1)
        sys.exit(0)

    if not args.dataset:
        print("ERROR: --dataset or --all required")
        sys.exit(1)

    if args.dir:
        pattern = os.path.join(args.dir, f"omtf_nano_{args.dataset}_*.root")
        files = sorted(glob.glob(pattern))[: args.max_files]
    elif args.files:
        files = args.files[: args.max_files]
    else:
        print("ERROR: provide --files or --dir")
        sys.exit(1)

    if not files:
        print(f"ERROR: no files found for {args.dataset}")
        sys.exit(1)

    ok = audit_dataset(args.dataset, files)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
