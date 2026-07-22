#!/usr/bin/env bash
###############################################################################
# collect_smoke_condor_results.sh — merge smoke_result.json files produced by
# condor/smoke_test_matrix.sub job runs into
# smoke_results/manifest_smoke/SUMMARY.jsonl.
#
# Usage: ./collect_smoke_condor_results.sh
#
# Safe to re-run: skips datasets already present in SUMMARY.jsonl unless
# their smoke_result.json is newer.
###############################################################################
set -uo pipefail

BASEDIR="/afs/cern.ch/user/${USER:0:1}/${USER}/omtf_dataset_production"
cd "${BASEDIR}"

OUTROOT="smoke_results/manifest_smoke"
SUMMARY="${OUTROOT}/SUMMARY.jsonl"

FOUND=0
APPENDED=0
for f in "${OUTROOT}"/*/smoke_result.json; do
  [[ -f "${f}" ]] || continue
  FOUND=$((FOUND+1))
  NAME=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['name'])" "${f}")
  if grep -q "\"name\": \"${NAME}\"" "${SUMMARY}" 2>/dev/null; then
    continue
  fi
  cat "${f}" >> "${SUMMARY}"
  echo >> "${SUMMARY}"
  APPENDED=$((APPENDED+1))
  echo "Appended: ${NAME}"
done

echo ""
echo "Found ${FOUND} smoke_result.json files, appended ${APPENDED} new entries to ${SUMMARY}"
