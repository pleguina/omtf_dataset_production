#!/usr/bin/env python3
"""Strict event-key join audit between OMTFAllInputTree and Nano Events.

Checks:
- reg_eventNum64 exists and matches exact 64-bit key join semantics
- join coverage from hits->nano and nano->hits
- duplicate normalized keys in both trees
- legacy reg_eventNum consistency with truncation (informational)
"""

import argparse
import json
from pathlib import Path

import numpy as np
import uproot


def to_set(r, l, e):
    return set(zip(r.tolist(), l.tolist(), e.tolist()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hits", required=True)
    ap.add_argument("--nano", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--require-hits-coverage", type=float, default=1.0)
    ap.add_argument("--require-nano-coverage", type=float, default=0.0)
    ap.add_argument("--require-dup-keys", type=int, default=0)
    args = ap.parse_args()

    report = {
        "inputs": {"hits": args.hits, "nano": args.nano},
        "status": "fail",
    }

    f_hits = uproot.open(args.hits)
    f_nano = uproot.open(args.nano)

    all_input = f_hits["simOmtfPhase2Digis/OMTFAllInputTree"]
    events = f_nano["Events"]

    required_hits = ["reg_runNum", "reg_lumiNum", "reg_eventNum64"]
    missing_hits = [b for b in required_hits if b not in all_input.keys()]
    if missing_hits:
        report["error"] = f"Missing required hits branches: {missing_hits}"
        Path(args.out).write_text(json.dumps(report, indent=2) + "\n")
        raise SystemExit(1)

    required_nano = ["run", "luminosityBlock", "event"]
    missing_nano = [b for b in required_nano if b not in events.keys()]
    if missing_nano:
        report["error"] = f"Missing required nano branches: {missing_nano}"
        Path(args.out).write_text(json.dumps(report, indent=2) + "\n")
        raise SystemExit(1)

    hr = all_input["reg_runNum"].array(library="np")
    hl = all_input["reg_lumiNum"].array(library="np")
    he = all_input["reg_eventNum64"].array(library="np")

    nr = events["run"].array(library="np")
    nl = events["luminosityBlock"].array(library="np")
    ne = events["event"].array(library="np")

    hkeys = to_set(hr, hl, he)
    nkeys = to_set(nr, nl, ne)
    inter = hkeys & nkeys

    # OMTFAllInputTree is one row per processor-window, so duplicates in pure
    # (run,lumi,event64) are expected. Enforce uniqueness on canonical window key.
    if all(b in all_input.keys() for b in ["reg_endcap", "reg_iProcessor", "reg_mtfType"]):
        hec = all_input["reg_endcap"].array(library="np")
        hpr = all_input["reg_iProcessor"].array(library="np")
        hmt = all_input["reg_mtfType"].array(library="np")
        hwin = set(zip(hr.tolist(), hl.tolist(), he.tolist(), hec.tolist(), hpr.tolist(), hmt.tolist()))
        dup_hits = int(len(hr) - len(hwin))
    else:
        dup_hits = int(len(hr) - len(hkeys))
    dup_nano = int(len(nr) - len(nkeys))

    hits_cov = float(len(inter) / len(hkeys)) if hkeys else 0.0
    nano_cov = float(len(inter) / len(nkeys)) if nkeys else 0.0

    legacy = {}
    if "reg_eventNum" in all_input.keys():
        e32 = all_input["reg_eventNum"].array(library="np")
        trunc = (he & np.uint64(0xFFFFFFFF)).astype(np.uint32)
        mism = int(np.sum(trunc != e32))
        legacy = {
            "reg_eventNum_present": True,
            "reg_eventNum_vs_truncated_event64_mismatches": mism,
        }
    else:
        legacy = {"reg_eventNum_present": False}

    status = "pass"
    if hits_cov < args.require_hits_coverage:
        status = "fail"
    if nano_cov < args.require_nano_coverage:
        status = "fail"
    if dup_hits > args.require_dup_keys or dup_nano > args.require_dup_keys:
        status = "fail"

    report.update(
        {
            "status": status,
            "hits_rows": int(len(hr)),
            "nano_rows": int(len(nr)),
            "hits_unique_keys": int(len(hkeys)),
            "nano_unique_keys": int(len(nkeys)),
            "joined_unique_keys": int(len(inter)),
            "hits_join_coverage": hits_cov,
            "nano_join_coverage": nano_cov,
            "duplicate_normalized_event_keys": {
                "hits": dup_hits,
                "nano": dup_nano,
            },
            "legacy_event_adapter": legacy,
        }
    )

    Path(args.out).write_text(json.dumps(report, indent=2) + "\n")
    print(f"Wrote {args.out} status={status}")
    raise SystemExit(0 if status == "pass" else 2)


if __name__ == "__main__":
    main()
