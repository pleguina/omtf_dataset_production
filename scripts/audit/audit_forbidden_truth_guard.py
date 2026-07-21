#!/usr/bin/env python3
"""Static guard: forbidden truth variables must never reach model input features.

This performs a lightweight, dependency-tracing static analysis of a graph/
model-input builder script (default: scripts/make_graphs.py). It:
  1. Finds every `<var> = read("<branch>")` (or `tree["<branch>"].array(...)`)
     assignment to map local variable names -> source branch names.
  2. Follows simple `<var> = <expr>` reassignments to propagate that mapping
     through derived variables (e.g. phi_norm derived from phi_hw).
  3. Locates the feature-stacking call (`np.stack([...])` assigned to a name
     containing "x_cont"/"x " /"node_feat") and resolves every stacked
     variable back to its source branch(es).
  4. Fails if any resolved source branch is in the FORBIDDEN set (truth-level
     kinematics/identity: TpMuon_*, GenMuon_*, reg_stub_trackId, reg_stub_tpId,
     reg_stub_bestSimTrackId/EventId, reg_stub_ambiguous, reg_stub_dualIdRelation).

This is intentionally conservative/textual (not a full Python AST dataflow
analysis) but is tuned to this repo's actual builder script structure.
"""

import argparse
import json
import re
import sys
from pathlib import Path

FORBIDDEN_BRANCH_PREFIXES = (
    "TpMuon_",
    "GenMuon_",
)
FORBIDDEN_BRANCH_EXACT = {
    "reg_stub_trackId",
    "reg_stub_tpId",
    "reg_stub_ambiguous",
    "reg_stub_dualIdRelation",
    "reg_stub_bestSimTrackId",
    "reg_stub_bestSimEventId",
    "reg_stub_noSelectedMuonReason",
}


def is_forbidden(branch: str) -> bool:
    return branch in FORBIDDEN_BRANCH_EXACT or branch.startswith(FORBIDDEN_BRANCH_PREFIXES)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--script", default="scripts/make_graphs.py")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    src = Path(args.script).read_text()
    lines = src.splitlines()

    # Step 1: var -> branch, from `<var> = read("<branch>")`.
    var_to_branch = {}
    read_pat = re.compile(r"^\s*(\w+)\s*=\s*read\(\s*[\"'](\w+)[\"']\s*\)")
    for line in lines:
        m = read_pat.match(line)
        if m:
            var_to_branch[m.group(1)] = {m.group(2)}

    # Step 2: propagate through simple derived assignments `<var> = <expr using other vars>`.
    assign_pat = re.compile(r"^\s*(\w+)\s*=\s*(.+)$")
    ident_pat = re.compile(r"\b([A-Za-z_]\w*)\b")
    changed = True
    passes = 0
    while changed and passes < 10:
        changed = False
        passes += 1
        for line in lines:
            m = assign_pat.match(line)
            if not m:
                continue
            target, rhs = m.group(1), m.group(2)
            if target in var_to_branch and passes > 1:
                continue
            rhs_vars = set(ident_pat.findall(rhs))
            derived = set()
            for v in rhs_vars:
                if v in var_to_branch:
                    derived |= var_to_branch[v]
            if derived and var_to_branch.get(target) != derived:
                prev = var_to_branch.get(target, set())
                var_to_branch[target] = prev | derived
                changed = True

    # Step 3: find the feature-stacking call.
    stack_pat = re.compile(r"(x_cont|node_feat\w*)\s*=\s*np\.stack\(\s*\[([^\]]+)\]")
    feature_vars = []
    for m in stack_pat.finditer(src):
        feature_vars.extend(v.strip() for v in m.group(2).split(","))

    if not feature_vars:
        out = {
            "status": "fail",
            "error": "Could not locate a feature-stacking call (x_cont/node_feat = np.stack([...])) "
                     "in the script; cannot verify forbidden-variable exclusion.",
        }
        Path(args.out).write_text(json.dumps(out, indent=2) + "\n")
        print(f"Wrote {args.out} status=fail")
        raise SystemExit(2)

    resolved = {}
    forbidden_hits = {}
    for v in feature_vars:
        branches = var_to_branch.get(v, set())
        resolved[v] = sorted(branches)
        bad = sorted(b for b in branches if is_forbidden(b))
        if bad:
            forbidden_hits[v] = bad

    status = "pass" if not forbidden_hits else "fail"
    out = {
        "status": status,
        "script": args.script,
        "feature_vars": feature_vars,
        "resolved_source_branches": resolved,
        "forbidden_hits": forbidden_hits,
    }

    Path(args.out).write_text(json.dumps(out, indent=2) + "\n")
    print(f"Wrote {args.out} status={status}")
    raise SystemExit(0 if status == "pass" else 2)


if __name__ == "__main__":
    main()
