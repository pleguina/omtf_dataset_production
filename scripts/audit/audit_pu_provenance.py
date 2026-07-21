#!/usr/bin/env python3
"""PU provenance audit with structural and semantic status.

Semantic validation remains provisional unless a controlled source-trace table
is supplied externally.
"""

import argparse
import json
from pathlib import Path

import numpy as np
import uproot


REQUIRED_STRUCTURAL = [
    "reg_pu_src_bx",
    "reg_pu_src_run",
    "reg_pu_src_lumi",
    "reg_pu_src_event",
    "reg_pu_src_eventHash",
    "reg_pu_src_occurrenceHash",
    "TpMuon_puSourceIdx",
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hits", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    t = uproot.open(args.hits)["simOmtfPhase2Digis/OMTFAllInputTree"]
    missing = [b for b in REQUIRED_STRUCTURAL if b not in t.keys()]

    out = {
        "structural_status": "pass",
        "semantic_validation_status": "provisional",
        "missing_branches": missing,
        "status": "pass",
    }

    if missing:
        out["structural_status"] = "fail"
        out["status"] = "fail"
        Path(args.out).write_text(json.dumps(out, indent=2) + "\n")
        print(f"Wrote {args.out} status={out['status']}")
        raise SystemExit(2)

    pu_idx = t["TpMuon_puSourceIdx"].array(library="np")
    src_bx = t["reg_pu_src_bx"].array(library="np")

    oob = 0
    for i in range(len(pu_idx)):
        nsrc = len(src_bx[i])
        for x in pu_idx[i]:
            xi = int(x)
            if xi < -1:
                oob += 1
            elif xi >= nsrc:
                oob += 1

    out["findings"] = {
        "rows": int(len(pu_idx)),
        "oob_puSourceIdx": int(oob),
    }

    if oob > 0:
        out["structural_status"] = "fail"
        out["status"] = "fail"

    Path(args.out).write_text(json.dumps(out, indent=2) + "\n")
    print(f"Wrote {args.out} status={out['status']}")
    raise SystemExit(0 if out["status"] == "pass" else 2)


if __name__ == "__main__":
    main()
