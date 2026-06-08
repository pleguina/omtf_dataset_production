#!/usr/bin/env python3
"""
scripts/audit/check_fragments_static.py

Static analysis of CMSSW generator fragment files for the G-campaign.

Parses each fragment and warns if:
  1. A single-muon dataset has more than one PartID entry.
  2. An eta filter is defined but NOT included in ProductionFilterSequence.
  3. AddAntiParticle=True is used unexpectedly.
    4. A hard-negative dataset (G7/G8/G9*/G10*) has eta overlapping the OMTF overlap region.
    5. A PU dataset (G2/G4/G5/G6/G8/G10*) has no corresponding DIGI/mixing config.
  6. ProductionFilterSequence references generator but not etaFilter when both
     are defined.

Usage:
    python3 check_fragments_static.py
    python3 check_fragments_static.py --fragments-dir path/to/fragments/
    python3 check_fragments_static.py --config-dir path/to/configs/

Exits with code 0 if all checks pass, 1 if any ERROR is found (WARNings are
non-fatal).
"""

import argparse
import ast
import os
import re
import sys

# ── Dataset metadata ──────────────────────────────────────────────────────────

# Single-muon datasets: PartID must have exactly ONE entry
SINGLE_MUON_DATASETS = {
    "G1", "G2", "G3", "G4", "G7", "G8",
    "G9_pos", "G9_neg", "G10_pos", "G10_neg",
}

# Multi-muon datasets and expected PartID count
EXPECTED_PART_IDS = {
    "G1": 1, "G2": 1, "G3": 1, "G4": 1,
    "G5": 2, "G6": 3,
    "G7": 1, "G8": 1,
    "G9_pos": 1, "G9_neg": 1,
    "G10_pos": 1, "G10_neg": 1,
}

# PU datasets: must have a corresponding DIGI config (*_cfg.py that includes PU)
PU_DATASETS = {"G2", "G4", "G5", "G6", "G8", "G10_pos", "G10_neg"}

# Hard-negative datasets: eta must NOT include the OMTF overlap region [0.82, 1.24]
HARD_NEG_DATASETS = {"G7", "G8", "G9_pos", "G9_neg", "G10_pos", "G10_neg"}

ETA_OVERLAP_MIN = 0.82
ETA_OVERLAP_MAX = 1.24

# ── Fragment file mapping ─────────────────────────────────────────────────────

FRAGMENT_FILES = {
    "G1": "G1_singlePromptOverlap.py",
    "G2": "G2_singlePromptOverlap_PU200.py",
    "G3": "G3_singleDisplacedOverlap.py",
    "G4": "G4_singleDisplacedOverlap_PU200.py",
    "G5": "G5_twoDisplacedOverlap_PU200.py",
    "G6": "G6_triPromptOverlap_PU200.py",
    "G7": "G7_hardNegLowEtaMuon.py",
    "G8": "G8_hardNegLowEtaMuon_PU200.py",
    "G9_pos": "G9_pos_hardNegHighEtaMuon.py",
    "G9_neg": "G9_neg_hardNegHighEtaMuon.py",
    "G10_pos": "G10_pos_hardNegHighEtaMuon_PU200.py",
    "G10_neg": "G10_neg_hardNegHighEtaMuon_PU200.py",
}

# ── Helpers ───────────────────────────────────────────────────────────────────

def extract_vint32(text, param):
    """Extract integer list from 'param = cms.vint32(...)' in text."""
    pattern = rf"{re.escape(param)}\s*=\s*cms\.vint32\(([^)]*)\)"
    m = re.search(pattern, text)
    if not m:
        return None
    inner = m.group(1)
    try:
        return [int(x.strip()) for x in inner.split(",") if x.strip()]
    except ValueError:
        return None


def extract_bool(text, param):
    """Extract bool value from 'param = cms.bool(True/False)' in text."""
    pattern = rf"{re.escape(param)}\s*=\s*cms\.bool\((True|False)\)"
    m = re.search(pattern, text)
    if not m:
        return None
    return m.group(1) == "True"


def extract_double(text, param):
    """Extract float from 'param = cms.double(...)' in text."""
    pattern = rf"{re.escape(param)}\s*=\s*cms\.double\(([^)]*)\)"
    m = re.search(pattern, text)
    if not m:
        return None
    try:
        return float(m.group(1).strip())
    except ValueError:
        return None


def has_eta_filter_defined(text):
    """True if 'etaFilter = cms.EDFilter(...)' appears in the fragment."""
    return bool(re.search(r"etaFilter\s*=\s*cms\.EDFilter\s*\(", text))


def filter_in_sequence(text):
    """True if ProductionFilterSequence includes etaFilter."""
    m = re.search(r"ProductionFilterSequence\s*=\s*cms\.Sequence\(([^)]*)\)", text)
    if not m:
        return False
    seq_body = m.group(1)
    return "etaFilter" in seq_body


def eta_overlaps_omtf(min_eta, max_eta):
    """True if the eta range [min_eta, max_eta] intersects the OMTF overlap band."""
    # Check positive overlap band [0.82, 1.24]
    if max_eta > ETA_OVERLAP_MIN and min_eta < ETA_OVERLAP_MAX:
        return True
    # Check negative overlap band [-1.24, -0.82]
    if min_eta < -ETA_OVERLAP_MIN and max_eta > -ETA_OVERLAP_MAX:
        return True
    return False


# ── Fragment checker ──────────────────────────────────────────────────────────

def check_fragment(tag, frag_path, config_dir):
    errors = []
    warnings = []
    infos = []

    if not os.path.isfile(frag_path):
        errors.append(f"Fragment file not found: {frag_path}")
        return errors, warnings, infos

    with open(frag_path) as fh:
        text = fh.read()

    fname = os.path.basename(frag_path)

    # ── Check 1: PartID count ──────────────────────────────────────────────
    part_ids = extract_vint32(text, "PartID")
    if part_ids is None:
        warnings.append(f"{fname}: could not parse PartID — skipping multiplicity check")
    else:
        expected_n = EXPECTED_PART_IDS.get(tag)
        if expected_n is not None and len(part_ids) != expected_n:
            if tag in SINGLE_MUON_DATASETS:
                errors.append(
                    f"{fname}: PartID has {len(part_ids)} entries {part_ids} but "
                    f"dataset {tag} is labelled single-muon (expected exactly 1)."
                )
            else:
                warnings.append(
                    f"{fname}: PartID has {len(part_ids)} entries {part_ids}, "
                    f"expected {expected_n} for {tag}."
                )
        else:
            infos.append(f"{fname}: PartID = {part_ids}  [OK]")

    # ── Check 2: etaFilter defined but not used ────────────────────────────
    has_filter = has_eta_filter_defined(text)
    filter_used = filter_in_sequence(text)
    if has_filter and not filter_used:
        errors.append(
            f"{fname}: etaFilter is defined but ProductionFilterSequence "
            f"does not include etaFilter."
        )
    elif has_filter and filter_used:
        infos.append(f"{fname}: etaFilter defined and used in ProductionFilterSequence  [OK]")
    elif not has_filter:
        # For overlap samples G1-G6, either explicit narrow eta or filter expected
        if tag in ("G1", "G2", "G3", "G4"):
            min_eta = extract_double(text, "MinEta")
            max_eta = extract_double(text, "MaxEta")
            if min_eta is not None and max_eta is not None:
                span = max_eta - min_eta
                if span > 0.6:  # wider than the 0.42-wide overlap band
                    warnings.append(
                        f"{fname}: no etaFilter defined but eta range [{min_eta}, {max_eta}] "
                        f"is wider than OMTF overlap band. Consider adding etaFilter."
                    )

    # ── Check 3: AddAntiParticle ───────────────────────────────────────────
    add_anti = extract_bool(text, "AddAntiParticle")
    if add_anti is True:
        warnings.append(
            f"{fname}: AddAntiParticle=True — verify this is intentional. "
            f"For single-muon samples this doubles the generated multiplicity."
        )

    # ── Check 4: Hard-negative eta should not overlap OMTF acceptance ─────
    if tag in HARD_NEG_DATASETS:
        min_eta = extract_double(text, "MinEta")
        max_eta = extract_double(text, "MaxEta")
        if min_eta is not None and max_eta is not None:
            if eta_overlaps_omtf(min_eta, max_eta):
                errors.append(
                    f"{fname}: hard-negative dataset {tag} has eta range "
                    f"[{min_eta}, {max_eta}] that overlaps the OMTF overlap "
                    f"acceptance [{ETA_OVERLAP_MIN}, {ETA_OVERLAP_MAX}]."
                )
            else:
                infos.append(
                    f"{fname}: hard-negative eta [{min_eta}, {max_eta}] does not "
                    f"overlap OMTF acceptance  [OK]"
                )

    # ── Check 5: PU dataset config existence ──────────────────────────────
    # NOTE: G*_cfg.py files are generated by scripts/generate_configs.sh
    # (requires cmsenv). A missing config before generation is a WARNING,
    # not a fragment defect. If the config exists, verify it has PU mixing.
    if tag in PU_DATASETS and config_dir:
        cfg_path = os.path.join(config_dir, f"{tag}_cfg.py")
        if not os.path.isfile(cfg_path):
            warnings.append(
                f"{fname}: no config found for PU dataset {tag} at {cfg_path}. "
                f"Run scripts/generate_configs.sh to create it before production."
            )
        else:
            # Check that the config actually contains PU mixing
            with open(cfg_path) as cf:
                cfg_text = cf.read()
            if "AVE_200_BX_25ns" not in cfg_text and "mix" not in cfg_text.lower():
                errors.append(
                    f"{tag}_cfg.py: PU dataset config does not appear to contain "
                    f"PU200 mixing (no 'AVE_200_BX_25ns' or 'mix' keyword found)."
                )
            else:
                infos.append(f"{tag}_cfg.py: PU200 mixing confirmed  [OK]")

    return errors, warnings, infos


# ── Main ──────────────────────────────────────────────────────────────────────

def parse_args():
    p = argparse.ArgumentParser(description="Static audit of G-campaign fragment files")
    p.add_argument("--fragments-dir", "-f",
                   default=os.path.join(os.path.dirname(__file__), "../../fragments"),
                   help="Directory containing fragment .py files")
    p.add_argument("--config-dir", "-c",
                   default=os.path.join(os.path.dirname(__file__), "../../configs"),
                   help="Directory containing cmsDriver-generated config files")
    p.add_argument("--tags", nargs="*",
                   help="Restrict to specific dataset tags, e.g. G1 G2 G8 G10_pos")
    p.add_argument("--quiet", "-q", action="store_true",
                   help="Suppress INFO lines, show only WARNs and ERRORs")
    return p.parse_args()


def main():
    args = parse_args()

    fragments_dir = os.path.realpath(args.fragments_dir)
    config_dir    = os.path.realpath(args.config_dir) if os.path.isdir(args.config_dir) else None

    tags = args.tags if args.tags else sorted(FRAGMENT_FILES.keys())

    total_errors = 0
    total_warnings = 0

    for tag in tags:
        frag_file = FRAGMENT_FILES.get(tag)
        if frag_file is None:
            print(f"[{tag}] SKIP: not in FRAGMENT_FILES mapping")
            continue

        frag_path = os.path.join(fragments_dir, frag_file)
        errors, warnings, infos = check_fragment(tag, frag_path, config_dir)

        if errors or warnings or infos:
            print(f"\n[{tag}]  {frag_file}")
        for msg in errors:
            print(f"  ERROR   {msg}")
            total_errors += 1
        for msg in warnings:
            print(f"  WARNING {msg}")
            total_warnings += 1
        if not args.quiet:
            for msg in infos:
                print(f"  INFO    {msg}")
        if not errors and not warnings:
            print(f"[{tag}]  {frag_file}  -> all checks passed")

    print(f"\n{'='*60}")
    print(f"  Errors:   {total_errors}")
    print(f"  Warnings: {total_warnings}")
    print(f"{'='*60}")

    if total_errors > 0:
        print("  RESULT: FAIL — fix errors before launching production")
        sys.exit(1)
    else:
        print("  RESULT: PASS")
        sys.exit(0)


if __name__ == "__main__":
    main()
