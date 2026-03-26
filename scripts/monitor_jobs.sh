#!/bin/bash
###############################################################################
# monitor_jobs.sh — Check status of OMTF HECIN production jobs
###############################################################################
set -euo pipefail

EOS_BASE="/eos/cms/store/user/${USER}/omtf_hecin_datasets"

echo "==================================================================="
echo "  OMTF HECIN Production — Job Monitor"
echo "  $(date)"
echo "==================================================================="
echo ""

# --- Condor queue ---
echo "--- HTCondor Queue ---"
condor_q "${USER}" 2>/dev/null || echo "(condor_q not available or no jobs)"
echo ""

# --- EOS output counts ---
echo "--- EOS Output Status ---"
echo "EOS base: ${EOS_BASE}/prod/"
echo ""

for DS in S1 S3 S4 B1 B2 B3; do
    DSPATH="${EOS_BASE}/prod/${DS}"
    if [ -d "${DSPATH}" ]; then
        NFILES=$(ls "${DSPATH}"/*.root 2>/dev/null | wc -l)
        if [ "${NFILES}" -gt 0 ]; then
            TOTAL_SIZE=$(du -sh "${DSPATH}" 2>/dev/null | awk '{print $1}')
        else
            TOTAL_SIZE="0"
        fi
        printf "  %-6s:  %5d files,  %s\n" "${DS}" "${NFILES}" "${TOTAL_SIZE}"
    else
        printf "  %-6s:  (directory not found)\n" "${DS}"
    fi
done

echo ""
echo "--- Commands ---"
echo "  condor_q ${USER}          # check running jobs"
echo "  condor_q ${USER} -hold    # check held jobs"
echo "  condor_tail <JOBID>       # stream job output"
echo "  condor_rm ${USER}         # remove all your jobs"
echo ""
