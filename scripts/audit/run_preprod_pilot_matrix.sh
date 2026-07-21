#!/usr/bin/env bash
set -euo pipefail

# Run a minimal pre-production pilot matrix using existing local test runner.
# This script does NOT submit condor jobs.

BASE="/afs/cern.ch/user/p/pleguina/omtf_dataset_production"
RUN_LOCAL="${BASE}/condor/run_local_test.sh"
OUTDIR="${BASE}/smoke_results/pilot_matrix_$(date +%Y%m%d_%H%M%S)"
NEVT="${1:-100}"
PROCID="${2:-0}"

mkdir -p "${OUTDIR}"

SAMPLES=(
  C1_prompt_pt2to5_overlap
  C7_disp_pt2to5_overlap
  C13_prompt_pt2to5_overlap_PU200
  C15_disp_pt2to5_overlap_PU200
  C27_mild_disp_pt2to5_overlap_PU200
  G17_twoDisplaced_overlap
  G5
  G8
  B4
)

printf "sample,status,log\n" > "${OUTDIR}/pilot_matrix.csv"

for s in "${SAMPLES[@]}"; do
  log="${OUTDIR}/${s}.log"
  if bash "${RUN_LOCAL}" "${s}" "${NEVT}" "${PROCID}" >"${log}" 2>&1; then
    printf "%s,OK,%s\n" "${s}" "${log}" >> "${OUTDIR}/pilot_matrix.csv"
  else
    printf "%s,FAIL,%s\n" "${s}" "${log}" >> "${OUTDIR}/pilot_matrix.csv"
  fi
done

echo "Pilot matrix summary: ${OUTDIR}/pilot_matrix.csv"
