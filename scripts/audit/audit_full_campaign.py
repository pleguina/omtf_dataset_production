#!/usr/bin/env python3
"""Aggregate pre-production audit runner.

Runs strict audits for one hits+nano pair and emits a combined JSON summary.
This is intended for pilot validation before broad condor submission.
"""

import argparse
import json
import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def run(cmd):
    p = subprocess.run(cmd, text=True, capture_output=True)
    return {
        "cmd": cmd,
        "returncode": p.returncode,
        "stdout": p.stdout,
        "stderr": p.stderr,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--hits", required=True)
    ap.add_argument("--nano", required=True)
    ap.add_argument("--outdir", required=True)
    args = ap.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    # Ensure manifest is readable and carries normative structure.
    manifest = json.loads(Path(args.manifest).read_text())
    if "authorizedDatasets" not in manifest:
        raise SystemExit("manifest missing authorizedDatasets")

    steps = [
        (
            "event_join",
            [
                "python3",
                str(REPO_ROOT / "scripts/audit/audit_event_join_exact.py"),
                "--hits",
                args.hits,
                "--nano",
                args.nano,
                "--out",
                str(outdir / "event_join_exact.json"),
            ],
        ),
        (
            "truth_unification",
            [
                "python3",
                str(REPO_ROOT / "scripts/audit/audit_truth_unification.py"),
                "--hits",
                args.hits,
                "--nano",
                args.nano,
                "--out",
                str(outdir / "truth_unification_matrix.json"),
            ],
        ),
        (
            "pu_provenance",
            [
                "python3",
                str(REPO_ROOT / "scripts/audit/audit_pu_provenance.py"),
                "--hits",
                args.hits,
                "--out",
                str(outdir / "pu_provenance_structural.json"),
            ],
        ),
        (
            "stub_vector_closure",
            [
                "python3",
                str(REPO_ROOT / "scripts/audit/audit_stub_vector_closure.py"),
                "--hits",
                args.hits,
                "--out",
                str(outdir / "stub_vector_closure.json"),
            ],
        ),
        (
            "forbidden_truth_guard",
            [
                "python3",
                str(REPO_ROOT / "scripts/audit/audit_forbidden_truth_guard.py"),
                "--script",
                str(REPO_ROOT / "scripts/make_graphs.py"),
                "--out",
                str(outdir / "forbidden_truth_guard.json"),
            ],
        ),
        (
            "b4_denominator",
            [
                "python3",
                str(REPO_ROOT / "scripts/audit/audit_b4_denominator.py"),
                "--hits",
                args.hits,
                "--nano",
                args.nano,
                "--out",
                str(outdir / "b4_rate_denominator.json"),
            ],
        ),
        (
            "v52_schema",
            [
                "python3",
                str(REPO_ROOT / "scripts/audit/validate_displaced_lane_v52_schema.py"),
                "--hits-file",
                args.hits,
                "--nano-file",
                args.nano,
                "--out",
                str(outdir / "v52_schema_validation.json"),
            ],
        ),
    ]

    summary = {
        "manifest": args.manifest,
        "hits": args.hits,
        "nano": args.nano,
        "results": {},
        "status": "pass",
    }

    for name, cmd in steps:
        r = run(cmd)
        summary["results"][name] = r
        if r["returncode"] != 0:
            summary["status"] = "fail"

    (outdir / "full_campaign_audit_summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"Wrote {(outdir / 'full_campaign_audit_summary.json')} status={summary['status']}")
    raise SystemExit(0 if summary["status"] == "pass" else 2)


if __name__ == "__main__":
    main()
