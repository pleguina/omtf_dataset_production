#!/usr/bin/env python3
"""B4 denominator/accounting audit.

Computes event-level accounting counters required for rate normalization.
"""

import argparse
import json
from pathlib import Path

import uproot


def unique_event_keys(tree, run_b, lumi_b, evt_b):
    r = tree[run_b].array(library="np")
    l = tree[lumi_b].array(library="np")
    e = tree[evt_b].array(library="np")
    return set(zip(r.tolist(), l.tolist(), e.tolist()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hits", required=True)
    ap.add_argument("--nano", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    f_hits = uproot.open(args.hits)
    f_nano = uproot.open(args.nano)

    all_input = f_hits["simOmtfPhase2Digis/OMTFAllInputTree"]
    hits_tree = f_hits["simOmtfPhase2Digis/OMTFHitsTree"]
    events = f_nano["Events"]

    gen_keys = unique_event_keys(events, "run", "luminosityBlock", "event")
    input_keys = unique_event_keys(all_input, "reg_runNum", "reg_lumiNum", "reg_eventNum64")

    if hits_tree.num_entries > 0:
        prop_keys = unique_event_keys(hits_tree, "runNum", "lumiNum", "eventNum64")
    else:
        prop_keys = set()

    out = {
        "nGeneratedEvents": int(len(gen_keys)),
        "nEventsWithOmtfInput": int(len(input_keys)),
        "nOmtfWindows": int(all_input.num_entries),
        "nProposalBearingEvents": int(len(prop_keys)),
        "nAcceptedEvents": int(len(gen_keys)),
        "status": "pass",
    }

    # Basic sanity for denominators.
    if out["nEventsWithOmtfInput"] > out["nGeneratedEvents"]:
        out["status"] = "fail"
    if out["nProposalBearingEvents"] > out["nGeneratedEvents"]:
        out["status"] = "fail"

    Path(args.out).write_text(json.dumps(out, indent=2) + "\n")
    print(f"Wrote {args.out} status={out['status']}")
    raise SystemExit(0 if out["status"] == "pass" else 2)


if __name__ == "__main__":
    main()
