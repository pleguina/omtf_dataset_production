#!/bin/bash
###############################################################################
# run_pipeline.sh — Master orchestrator for OMTF HECIN dataset production
#
# Executes phases in order:
#   Phase 1: CMSSW setup
#   Phase 2: Config generation
#   Phase 3: Validation (1000 events per dataset)
#   Phase 4: Storage estimation
#   Phase 5: Create Condor submit files
#   Phase 6: (Optional) Submit production
#
# Usage:
#   ./run_pipeline.sh              — Run phases 1–5 (stops before submission)
#   ./run_pipeline.sh --submit     — Run phases 1–6 (includes HTCondor submit)
#   ./run_pipeline.sh --phase N    — Run only phase N
###############################################################################
set -euo pipefail

BASEDIR="$HOME/omtf_hecin_dataset_production"
SCRIPTS="${BASEDIR}/scripts"
DO_SUBMIT=false
SINGLE_PHASE=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --submit)  DO_SUBMIT=true; shift ;;
        --phase)   SINGLE_PHASE="$2"; shift 2 ;;
        *)         echo "Unknown option: $1"; exit 1 ;;
    esac
done

run_phase() {
    local PHASE=$1
    local DESC=$2
    local SCRIPT=$3

    echo ""
    echo "###################################################################"
    echo "#  PHASE ${PHASE}: ${DESC}"
    echo "###################################################################"
    echo ""

    chmod +x "${SCRIPT}"
    bash "${SCRIPT}"

    echo ""
    echo ">>> Phase ${PHASE} complete."
    echo ""
}

# If single phase requested
if [ "${SINGLE_PHASE}" -ne 0 ] 2>/dev/null; then
    case "${SINGLE_PHASE}" in
        1) run_phase 1 "CMSSW Setup"            "${SCRIPTS}/setup_cmssw.sh"       ;;
        2) run_phase 2 "Config Generation"       "${SCRIPTS}/generate_configs.sh"  ;;
        3) run_phase 3 "Validation (1000 evts)"  "${SCRIPTS}/run_validation.sh"    ;;
        4) run_phase 4 "Storage Estimation"      "${SCRIPTS}/estimate_storage.sh"  ;;
        5) run_phase 5 "Create Condor Sub Files" "${SCRIPTS}/create_condor_subs.sh";;
        6) run_phase 6 "Submit Production"       "${SCRIPTS}/submit_all.sh"        ;;
        *) echo "Invalid phase: ${SINGLE_PHASE}"; exit 1 ;;
    esac
    exit 0
fi

# Full pipeline
echo "==================================================================="
echo "  OMTF HECIN Dataset Production Pipeline"
echo "  Working dir: ${BASEDIR}"
echo "  Date:        $(date)"
echo "  User:        ${USER}"
echo "==================================================================="

run_phase 1 "CMSSW Setup"            "${SCRIPTS}/setup_cmssw.sh"
run_phase 2 "Config Generation"       "${SCRIPTS}/generate_configs.sh"
run_phase 3 "Validation (1000 evts)"  "${SCRIPTS}/run_validation.sh"
run_phase 4 "Storage Estimation"      "${SCRIPTS}/estimate_storage.sh"
run_phase 5 "Create Condor Sub Files" "${SCRIPTS}/create_condor_subs.sh"

if [ "${DO_SUBMIT}" = true ]; then
    run_phase 6 "Submit Production"   "${SCRIPTS}/submit_all.sh"
fi

echo ""
echo "==================================================================="
echo "  Pipeline finished at $(date)"
echo "==================================================================="
echo ""

if [ "${DO_SUBMIT}" = false ]; then
    echo "Production was NOT submitted. To submit, run:"
    echo ""
    echo "  bash ${SCRIPTS}/submit_all.sh"
    echo ""
    echo "Or re-run the full pipeline with --submit:"
    echo ""
    echo "  bash ${BASEDIR}/scripts/run_pipeline.sh --submit"
fi
