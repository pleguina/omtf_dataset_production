#!/usr/bin/env python3
"""Stub-vector length-closure audit.

OMTFAllInputTree stores per-primitive fields as parallel jagged vectors
(reg_stub_layer, reg_stub_phiHw, reg_stub_trackId, ...). Every parallel vector
MUST have exactly the same per-row (per processor-window) length as
reg_stub_layer, or downstream consumers (graph builder, audits) silently
misalign primitives across arrays. This script enforces that closure.
"""

import argparse
import json
from pathlib import Path

import uproot

# Every reg_stub_* branch that is expected to be parallel to reg_stub_layer.
# Add new stub-level branches here when the schema grows.
PARALLEL_STUB_BRANCHES = [
    "reg_stub_phiHw",
    "reg_stub_phiBHw",
    "reg_stub_etaHw",
    "reg_stub_r",
    "reg_stub_quality",
    "reg_stub_bx",
    "reg_stub_sourceBx",
    "reg_stub_trackId",
    "reg_stub_tpId",
    "reg_stub_ambiguous",
    "reg_stub_primitiveId",
    "reg_stub_sourceCollection",
    "reg_stub_sourceDetId",
    "reg_stub_sourceIndex",
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hits", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    t = uproot.open(args.hits)["simOmtfPhase2Digis/OMTFAllInputTree"]

    if "reg_stub_layer" not in t.keys():
        out = {
            "status": "fail",
            "error": "reg_stub_layer (reference stub-vector) missing from OMTFAllInputTree.",
        }
        Path(args.out).write_text(json.dumps(out, indent=2) + "\n")
        print(f"Wrote {args.out} status=fail")
        raise SystemExit(2)

    ref = t["reg_stub_layer"].array(library="np")
    ref_lens = [len(x) for x in ref]

    present = [b for b in PARALLEL_STUB_BRANCHES if b in t.keys()]
    missing = [b for b in PARALLEL_STUB_BRANCHES if b not in t.keys()]

    mismatches = {}
    for b in present:
        arr = t[b].array(library="np")
        if len(arr) != len(ref):
            mismatches[b] = f"row count {len(arr)} != reference row count {len(ref)}"
            continue
        bad_rows = [i for i in range(len(ref)) if len(arr[i]) != ref_lens[i]]
        if bad_rows:
            mismatches[b] = {
                "n_bad_rows": len(bad_rows),
                "example_rows": bad_rows[:5],
            }

    status = "pass" if not mismatches else "fail"
    out = {
        "status": status,
        "reference_branch": "reg_stub_layer",
        "rows": int(len(ref)),
        "checked_branches": present,
        "missing_branches": missing,
        "length_mismatches": mismatches,
    }

    Path(args.out).write_text(json.dumps(out, indent=2) + "\n")
    print(f"Wrote {args.out} status={status}")
    raise SystemExit(0 if status == "pass" else 2)


if __name__ == "__main__":
    main()
