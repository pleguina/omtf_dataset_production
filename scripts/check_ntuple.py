#!/usr/bin/env python3
"""
check_ntuple.py — Inspect OMTF Phase-2 hit ntuples produced by the smoke test.

Usage:
    python3 scripts/check_ntuple.py test/smoke/        # all datasets
    python3 scripts/check_ntuple.py test/smoke/S1/     # single dataset
    python3 scripts/check_ntuple.py test/smoke/S1/omtf_hits.root  # single file
"""
import sys
import os
import argparse
from pathlib import Path


TREE_PATH = "simOmtfPhase2Digis/OMTFHitsTree"

# DataROOTDumper2 format — one entry per OMTF candidate matched to a gen muon.
# `hits` is std::vector<uint64_t> with packed stub data (layer|qual|etaHw|valid|deltaR|phiDist).

REQUIRED_BRANCHES = [
    "hits",             # vector<uint64>: packed stubs for this candidate
    "muonPt",           # gen muon pT  [GeV]
    "muonEta",          # gen muon eta
    "muonDxy",          # gen muon transverse impact parameter [cm]
    "muonRho",          # gen muon transverse decay radius [cm]
    "omtfPt",           # OMTF candidate pT  [GeV]
    "omtfQuality",      # OMTF quality (0-15; ≥12 = good)
    "omtfFiredLayers",  # bitmask of fired logic layers (bits 0-17)
]

DESIRED_BRANCHES = [
    "muonPhi",
    "muonCharge",
    "omtfProcessor",
    "omtfScore",
    "deltaEta",
    "deltaPhi",
    "parentPdgId",
    "vertexEta",
    "vertexPhi",
]

GEN_BRANCHES: list[str] = []  # all gen info is now in REQUIRED / DESIRED above

WARN_COLOUR = "\033[33m"
PASS_COLOUR = "\033[32m"
FAIL_COLOUR = "\033[31m"
INFO_COLOUR = "\033[36m"
RESET = "\033[0m"

try:
    import ROOT  # noqa: F401
    ROOT.gROOT.SetBatch(True)
    ROOT.RooMsgService.instance().setStreamStatus(0, False)
    ROOT.RooMsgService.instance().setStreamStatus(1, False)
    ROOT.gErrorIgnoreLevel = ROOT.kError
    HAS_ROOT = True
except ImportError:
    HAS_ROOT = False

try:
    import uproot
    HAS_UPROOT = True
except ImportError:
    HAS_UPROOT = False


def find_root_files(path_arg: str) -> list[Path]:
    p = Path(path_arg)
    if p.is_file() and p.suffix == ".root":
        return [p]
    if p.is_dir():
        files = sorted(p.rglob("omtf_hits.root"))
        return files
    print(f"[ERROR] Not a file or directory: {path_arg}")
    sys.exit(1)


def _popcount(x: int) -> int:
    """Count set bits (fired layers) in an integer bitmask."""
    return bin(x).count('1')


def inspect_with_uproot(rfile: Path) -> dict:
    import uproot
    import numpy as np

    result = {
        "n_candidates": 0,
        "branches_found": [],
        "branches_missing_required": [],
        "branches_missing_desired": [],
        "branches_missing_gen": [],
        # per-candidate stats
        "mean_hits_per_candidate": 0.0,
        "mean_fired_layers": 0.0,
        "stub_det_counts": {},          # {DT: N, CSC: N, RPC: N}
        # gen muon stats
        "gen_pt_range": None,
        "muon_rho_mean": None,
        "muon_rho_max": None,
        # OMTF performance
        "omtf_efficiency": None,        # fraction with quality >= 12
        "displaced_fraction": None,     # fraction |dxy| > 1 cm
        "errors": [],
    }

    try:
        with uproot.open(str(rfile)) as f:
            if TREE_PATH not in f:
                result["errors"].append(f"Tree '{TREE_PATH}' not found. Keys: {list(f.keys())}")
                return result

            tree = f[TREE_PATH]
            n = tree.num_entries
            result["n_candidates"] = n
            all_branches = set(tree.keys())
            result["branches_found"] = sorted(all_branches)

            result["branches_missing_required"] = [b for b in REQUIRED_BRANCHES if b not in all_branches]
            result["branches_missing_desired"]  = [b for b in DESIRED_BRANCHES  if b not in all_branches]
            result["branches_missing_gen"]       = []  # no separate gen branches in this format

            if n == 0:
                return result

            cap = min(n, 2000)

            # ---- hits (vector<uint64>) ------------------------------------
            if "hits" in all_branches:
                hits_arr = tree["hits"].array(library="np", entry_stop=cap)
                n_hits = []
                det_counts = {"DT": 0, "CSC": 0, "RPC": 0}
                for ev_hits in hits_arr:
                    n_hits.append(len(ev_hits))
                    for h in ev_hits:
                        layer = int(np.array(int(h) & 0xFF, dtype=np.uint8).view(np.int8))
                        if layer < 6:
                            det_counts["DT"] += 1
                        elif layer < 10:
                            det_counts["CSC"] += 1
                        else:
                            det_counts["RPC"] += 1
                result["mean_hits_per_candidate"] = float(np.mean(n_hits)) if n_hits else 0.0
                result["stub_det_counts"] = det_counts

            # ---- fired layers bitmask ------------------------------------
            if "omtfFiredLayers" in all_branches:
                fired = tree["omtfFiredLayers"].array(library="np", entry_stop=cap)
                result["mean_fired_layers"] = float(np.mean([_popcount(int(x)) for x in fired]))

            # ---- gen muon pT ---------------------------------------------
            if "muonPt" in all_branches:
                pts = tree["muonPt"].array(library="np", entry_stop=cap)
                pts = np.asarray(pts, dtype=np.float32)
                result["gen_pt_range"] = (float(pts.min()), float(pts.max()))

            # ---- displacement -------------------------------------------
            if "muonRho" in all_branches:
                rho = np.asarray(tree["muonRho"].array(library="np", entry_stop=cap), dtype=np.float32)
                result["muon_rho_mean"] = float(rho.mean())
                result["muon_rho_max"]  = float(rho.max())

            if "muonDxy" in all_branches:
                dxy = np.asarray(tree["muonDxy"].array(library="np", entry_stop=cap), dtype=np.float32)
                result["displaced_fraction"] = float(np.mean(np.abs(dxy) > 1.0))

            # ---- OMTF efficiency ----------------------------------------
            if "omtfQuality" in all_branches:
                qual = np.asarray(tree["omtfQuality"].array(library="np", entry_stop=cap), dtype=np.int32)
                result["omtf_efficiency"] = float(np.mean(qual >= 12))

    except Exception as exc:
        result["errors"].append(str(exc))

    return result


def colour(s: str, c: str) -> str:
    if sys.stdout.isatty():
        return f"{c}{s}{RESET}"
    return s


def print_dataset_report(ds_name: str, rfile: Path, info: dict, out_lines: list[str]):
    def emit(line=""):
        print(line)
        out_lines.append(line)

    emit(f"\n{'='*60}")
    emit(f"  {ds_name}   {rfile}")
    emit(f"{'='*60}")

    if info["errors"]:
        for err in info["errors"]:
            emit(colour(f"  [FAIL] {err}", FAIL_COLOUR))
        return "FAIL"

    emit(f"  Candidates   : {info['n_candidates']}")
    emit(f"  Mean hits/candidate : {info['mean_hits_per_candidate']:.2f}")
    emit(f"  Mean fired layers   : {info['mean_fired_layers']:.2f}")

    if info["stub_det_counts"]:
        emit(f"  Stub detector counts : {info['stub_det_counts']}")

    if info["gen_pt_range"]:
        emit(f"  Gen-pT range : [{info['gen_pt_range'][0]:.1f}, {info['gen_pt_range'][1]:.1f}] GeV")

    if info["muon_rho_mean"] is not None:
        emit(f"  muonRho (cm) : mean={info['muon_rho_mean']:.1f}  max={info['muon_rho_max']:.1f}")

    if info["displaced_fraction"] is not None:
        emit(f"  |dxy|>1cm fraction  : {info['displaced_fraction']:.3f}")

    if info["omtf_efficiency"] is not None:
        emit(f"  OMTF eff (qual≥12)  : {info['omtf_efficiency']:.3f}")

    status = "PASS"
    if info["branches_missing_required"]:
        emit(colour(f"  [FAIL] Missing REQUIRED branches: {info['branches_missing_required']}", FAIL_COLOUR))
        status = "FAIL"
    else:
        emit(colour(f"  [OK] All {len(REQUIRED_BRANCHES)} required branches present", PASS_COLOUR))

    if info["branches_missing_desired"]:
        emit(colour(f"  [WARN] Missing desired branches: {info['branches_missing_desired']}", WARN_COLOUR))
        if status == "PASS":
            status = "WARN"
    else:
        emit(colour(f"  [OK] All {len(DESIRED_BRANCHES)} desired branches present", PASS_COLOUR))

    emit(f"\n  All branches found ({len(info['branches_found'])}):")
    for b in info["branches_found"]:
        emit(f"    {b}")

    return status


def main():
    parser = argparse.ArgumentParser(description="Inspect OMTF Phase-2 hit ntuples")
    parser.add_argument("path", help="Directory of smoke outputs OR single .root file")
    parser.add_argument("--no-colour", action="store_true", help="Disable terminal colours")
    args = parser.parse_args()

    if not HAS_UPROOT:
        print("[ERROR] uproot not available. Install with: pip install uproot awkward")
        sys.exit(1)

    root_files = find_root_files(args.path)
    if not root_files:
        print(f"[ERROR] No omtf_hits.root files found under {args.path}")
        sys.exit(1)

    print(f"Found {len(root_files)} ntuple(s) to inspect.")

    out_lines: list[str] = []
    results: dict[str, str] = {}

    for rfile in root_files:
        # Derive dataset name from directory hierarchy: .../smoke/S1/omtf_hits.root
        ds_name = rfile.parent.name if rfile.name == "omtf_hits.root" else rfile.stem
        info = inspect_with_uproot(rfile)
        status = print_dataset_report(ds_name, rfile, info, out_lines)
        results[ds_name] = status

    # Final summary
    print("\n" + "="*60)
    print("  NTUPLE CHECK SUMMARY")
    print("="*60)
    out_lines.append("\n" + "="*60)
    out_lines.append("  NTUPLE CHECK SUMMARY")
    out_lines.append("="*60)

    n_pass = sum(1 for s in results.values() if s == "PASS")
    n_warn = sum(1 for s in results.values() if s == "WARN")
    n_fail = sum(1 for s in results.values() if s == "FAIL")

    for ds, status in sorted(results.items()):
        c = PASS_COLOUR if status == "PASS" else (WARN_COLOUR if status == "WARN" else FAIL_COLOUR)
        line = f"  {colour(status, c):8s}  {ds}"
        print(line)
        out_lines.append(f"  {status:8s}  {ds}")

    summary_line = f"\nPASS={n_pass}  WARN={n_warn}  FAIL={n_fail}  (of {len(results)} datasets)"
    print(summary_line)
    out_lines.append(summary_line)

    # Save plain-text report alongside results
    report_path = Path(args.path) if Path(args.path).is_dir() else Path(args.path).parent
    report_file = report_path / "check_results.txt"
    with open(report_file, "w") as f:
        f.write("\n".join(out_lines) + "\n")
    print(f"\nReport saved: {report_file}")

    sys.exit(0 if n_fail == 0 else 1)


if __name__ == "__main__":
    main()
