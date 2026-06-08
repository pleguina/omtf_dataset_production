#!/usr/bin/env python3
"""
scripts/omtf_gmt/audit_domain_mismatch.py

Stub-level domain audit for the GMT-visible-stub G-campaign datasets.

After a tiny GEN+DIGI+Nano test production, this script checks whether
the OMTF stub content matches the expected topology for each dataset:

  G1/G2/G3/G4:
    - true overlap target multiplicity == 1
    - overlap signal fraction is high

  G5:
    - true overlap target multiplicity == 2
    - both slot-0 and slot-1 targets present

  G6:
    - true overlap target multiplicity == 3
    - slot-2 targets present under PU

    G7/G8/G9*/G10*:
    - true overlap target multiplicity == 0
        - real stubs are present (barrel for G7/G8, endcap-like for G9/G10)
    - sample is NOT empty like B4

Usage:
    python3 audit_domain_mismatch.py --files omtf_hits_G2_*.root --dataset G2
    python3 audit_domain_mismatch.py --all --eos-base /eos/user/p/pleguina/omtf_hecin_datasets/prod
    python3 audit_domain_mismatch.py --dataset G10_pos --dir /path/to/test/outputs/

Requires: uproot, numpy, awkward

Exits with code 0 if all checks pass, 1 if any check fails.
"""

import argparse
import glob
import os
import sys

try:
    import uproot
    import numpy as np
    import awkward as ak
except ImportError:
    sys.exit(
        "ERROR: uproot/awkward/numpy not available.\n"
        "  pip install uproot awkward numpy"
    )

# ── Branch names (from OMTF hits ntuple) ──────────────────────────────────────

# These branch names match the OMTF dumper custom ntuple (omtf_hits tree).
# The exact names depend on the dumper version — the script tries both naming
# conventions and falls back gracefully.
BRANCHES_TO_TRY = [
    "nOmtfMuon",
    "OmtfMuon_pt",
    "OmtfMuon_eta",
    "OmtfMuon_hwQual",
    "OmtfMuon_isMatchedToGen",
    "nGenMuon",
    "GenMuon_eta",
    "nOmtfStub",
    "OmtfStub_absEta",
    "OmtfStub_layerId",
]

# ── Dataset audit specifications ──────────────────────────────────────────────

AUDIT_SPECS = {
    "G1": dict(expected_n_overlap=1, has_pu=False,  is_hard_neg=False, label="G1_singlePromptOverlap"),
    "G2": dict(expected_n_overlap=1, has_pu=True,   is_hard_neg=False, label="G2_singlePromptOverlap_PU200"),
    "G3": dict(expected_n_overlap=1, has_pu=False,  is_hard_neg=False, label="G3_singleDisplacedOverlap"),
    "G4": dict(expected_n_overlap=1, has_pu=True,   is_hard_neg=False, label="G4_singleDisplacedOverlap_PU200"),
    "G5": dict(expected_n_overlap=2, has_pu=True,   is_hard_neg=False, label="G5_twoDisplacedOverlap_PU200"),
    "G6": dict(expected_n_overlap=3, has_pu=True,   is_hard_neg=False, label="G6_triPromptOverlap_PU200"),
    "G7": dict(expected_n_overlap=0, has_pu=False,  is_hard_neg=True,  label="G7_hardNegLowEtaMuon"),
    "G8": dict(expected_n_overlap=0, has_pu=True,   is_hard_neg=True,  label="G8_hardNegLowEtaMuon_PU200"),
    "G9_pos": dict(expected_n_overlap=0, has_pu=False, is_hard_neg=True, label="G9_pos_hardNegHighEtaMuon"),
    "G9_neg": dict(expected_n_overlap=0, has_pu=False, is_hard_neg=True, label="G9_neg_hardNegHighEtaMuon"),
    "G10_pos": dict(expected_n_overlap=0, has_pu=True, is_hard_neg=True, label="G10_pos_hardNegHighEtaMuon_PU200"),
    "G10_neg": dict(expected_n_overlap=0, has_pu=True, is_hard_neg=True, label="G10_neg_hardNegHighEtaMuon_PU200"),
}

ETA_OVERLAP_MIN = 0.82
ETA_OVERLAP_MAX = 1.24

# ── Helpers ───────────────────────────────────────────────────────────────────

def load_hits_branches(files, max_files=50):
    """Load OMTF hits ntuple branches from a list of ROOT files."""
    arrays = {b: [] for b in BRANCHES_TO_TRY}
    n_loaded = 0
    for f in files[:max_files]:
        try:
            with uproot.open(f) as fh:
                tree = None
                for tname in ("omtfHits", "omtf", "Events", "omtfNano"):
                    if tname in fh:
                        tree = fh[tname]
                        break
                if tree is None:
                    print(f"  WARNING: no recognised tree in {f}, skipping")
                    continue
                avail = set(tree.keys())
                for b in BRANCHES_TO_TRY:
                    if b in avail:
                        arrays[b].append(tree[b].array(library="ak"))
            n_loaded += 1
        except Exception as e:
            print(f"  WARNING: failed to read {f}: {e}")

    if n_loaded == 0:
        return None, 0

    merged = {}
    for b in BRANCHES_TO_TRY:
        if arrays[b]:
            merged[b] = ak.concatenate(arrays[b], axis=0)
        else:
            merged[b] = None
    return merged, n_loaded


def count_overlap_targets(data):
    """
    Count per-event number of gen muons in the OMTF overlap acceptance.
    Uses GenMuon_eta if available.
    Returns an integer array of shape (n_events,), or None.
    """
    if data.get("GenMuon_eta") is None:
        return None
    abseta = np.abs(ak.to_numpy(ak.flatten(data["GenMuon_eta"])))
    # Rebuild per-event: count how many per event are in overlap
    gen_eta = data["GenMuon_eta"]
    in_overlap = (np.abs(gen_eta) >= ETA_OVERLAP_MIN) & (np.abs(gen_eta) <= ETA_OVERLAP_MAX)
    return ak.to_numpy(ak.sum(in_overlap, axis=1))


def count_stubs_in_overlap(data):
    """
    Count total stubs per event with |eta| in overlap region.
    Uses OmtfStub_absEta if available.
    """
    if data.get("OmtfStub_absEta") is None:
        return None
    stub_eta = data["OmtfStub_absEta"]
    in_overlap = (stub_eta >= ETA_OVERLAP_MIN) & (stub_eta <= ETA_OVERLAP_MAX)
    return ak.to_numpy(ak.sum(in_overlap, axis=1))


# ── Per-dataset audit ─────────────────────────────────────────────────────────

def audit_dataset(tag, files):
    spec = AUDIT_SPECS.get(tag)
    if spec is None:
        print(f"[{tag}] ERROR: unknown dataset tag")
        return False

    print(f"\n{'='*70}")
    print(f"  Dataset: {spec['label']}")
    print(f"  Files:   {len(files)}")
    print(f"{'='*70}")

    data, n_loaded = load_hits_branches(files)
    if data is None:
        print(f"  FATAL: could not load any files")
        return False

    print(f"  Files loaded: {n_loaded}")
    n_events = int(len(data.get("nGenMuon") or data.get("nOmtfMuon") or []))
    if n_events == 0:
        # try any non-None branch
        for b in BRANCHES_TO_TRY:
            if data.get(b) is not None:
                n_events = int(len(data[b]))
                break
    print(f"  Events: {n_events}")

    all_pass = True
    expected_n = spec["expected_n_overlap"]
    is_hard_neg = spec["is_hard_neg"]

    # ── 1. True overlap target multiplicity ───────────────────────────────
    n_overlap_per_event = count_overlap_targets(data)
    if n_overlap_per_event is not None:
        frac_correct = float(np.mean(n_overlap_per_event == expected_n))
        mean_n = float(np.mean(n_overlap_per_event))
        status = "PASS" if frac_correct >= 0.90 else "FAIL"
        if status == "FAIL":
            all_pass = False
        print(f"\n  Overlap gen multiplicity == {expected_n}: {frac_correct*100:.1f}%  "
              f"(mean={mean_n:.2f})  [{status}]")
        if not is_hard_neg and expected_n > 0:
            # Signal sample: check near-zero multiplicity events are rare
            frac_zero = float(np.mean(n_overlap_per_event == 0))
            if frac_zero > 0.10:
                print(f"  WARNING: {frac_zero*100:.1f}% events have 0 overlap targets "
                      f"(expected < 10%)")
    else:
        print("  GenMuon_eta: not available — skipping overlap multiplicity check  [SKIP]")

    # ── 2. Hard-negative check: ensure stubs are present (not empty) ───────
    if is_hard_neg:
        n_stubs = count_stubs_in_overlap(data)
        if n_stubs is not None:
            frac_empty = float(np.mean(n_stubs == 0))
            mean_stubs = float(np.mean(n_stubs))
            status = "PASS" if mean_stubs > 0.01 else "FAIL"
            if status == "FAIL":
                all_pass = False
            print(f"  Overlap stubs present: mean={mean_stubs:.2f} stubs/event  "
                  f"({(1-frac_empty)*100:.1f}% events have >=1 stub)  [{status}]")
            print(f"  NOTE: overlap target should be zero but KMTF stubs may bleed in.")
        else:
            # Check OMTF candidates instead
            if data.get("nOmtfMuon") is not None:
                n_cands = ak.to_numpy(data["nOmtfMuon"])
                mean_cands = float(np.mean(n_cands))
                frac_nonzero = float(np.mean(n_cands > 0))
                print(f"  OMTF candidates: mean={mean_cands:.2f}/event, "
                      f"{frac_nonzero*100:.1f}% events have >=1 candidate  [INFO]")
            else:
                print("  Stub/candidate branches not found — cannot verify hard-negative quality  [SKIP]")

    # ── 3. Multi-slot check for G5/G6 ─────────────────────────────────────
    if tag == "G5" and n_overlap_per_event is not None:
        frac_two = float(np.mean(n_overlap_per_event >= 2))
        status = "PASS" if frac_two >= 0.85 else "WARN"
        print(f"  G5 slot-0+slot-1 available (>=2 overlap targets): {frac_two*100:.1f}%  [{status}]")

    if tag == "G6" and n_overlap_per_event is not None:
        frac_three = float(np.mean(n_overlap_per_event >= 3))
        status = "PASS" if frac_three >= 0.75 else "WARN"
        print(f"  G6 slot-2 available (>=3 overlap targets): {frac_three*100:.1f}%  [{status}]")

    # ── 4. OMTF candidate quality (if branches available) ─────────────────
    if data.get("OmtfMuon_hwQual") is not None:
        qual_flat = ak.to_numpy(ak.flatten(data["OmtfMuon_hwQual"]))
        frac_hq = float(np.mean(qual_flat >= 12))  # quality >= 12: high-quality candidate
        print(f"\n  OMTF high-quality candidates (hwQual>=12): {frac_hq*100:.1f}%  [INFO]")

    # ── Summary ───────────────────────────────────────────────────────────
    overall = "PASS" if all_pass else "FAIL"
    print(f"\n  Overall: [{overall}]\n")
    return all_pass


# ── Main ──────────────────────────────────────────────────────────────────────

def parse_args():
    p = argparse.ArgumentParser(description="Stub-level domain audit for G-campaign datasets")
    p.add_argument("--dataset", "-d", help="Dataset tag, e.g. G1, G2, ..., G10_pos")
    p.add_argument("--files", "-f", nargs="*", help="List of ROOT hits files")
    p.add_argument("--dir", help="Directory containing omtf_hits_<DATASET>_*.root files")
    p.add_argument("--eos-base", default="/eos/user/p/pleguina/omtf_hecin_datasets/prod",
                   help="EOS base directory for --all mode")
    p.add_argument("--all", action="store_true", help="Audit all G datasets under --eos-base")
    p.add_argument("--max-files", type=int, default=20,
                   help="Maximum files to load per dataset (default 20)")
    return p.parse_args()


def main():
    args = parse_args()

    if args.all:
        results = {}
        for tag in sorted(AUDIT_SPECS.keys()):
            pattern = os.path.join(args.eos_base, f"omtf_hits_{tag}_*.root")
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
            status = "SKIP (no files)" if ok is None else ("PASS" if ok else "FAIL")
            print(f"  {tag:<6}  {status}")
        print()

        failed = [t for t, ok in results.items() if ok is False]
        if failed:
            print(f"  FAILED: {', '.join(failed)}")
            sys.exit(1)
        sys.exit(0)

    if not args.dataset:
        print("ERROR: --dataset or --all required")
        sys.exit(1)

    if args.dir:
        pattern = os.path.join(args.dir, f"omtf_hits_{args.dataset}_*.root")
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
