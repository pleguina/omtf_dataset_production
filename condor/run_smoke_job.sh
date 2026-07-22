#!/bin/bash
###############################################################################
# run_smoke_job.sh — HTCondor worker for a single manifest smoke test.
#
# Runs a small local cmsRun (via run_local_test.sh) on the assigned worker
# node's own scratch/tmp (NOT the lxplus login node), then runs the
# structural audit suite and emits a small smoke_result.json in the job's
# HTCondor scratch directory (transferred back to the submission side via
# transfer_output_files / initialdir).
#
# Usage:  run_smoke_job.sh  <NAME>  <CFGBASE>  [NEVENTS]
#   NAME    - manifest dataset name (used only for labeling smoke_result.json)
#   CFGBASE - configs/<CFGBASE>_cfg.py basename to actually run
#   NEVENTS - events for the smoke cmsRun (default 30)
###############################################################################
set -uo pipefail

NAME="${1:?Usage: run_smoke_job.sh NAME CFGBASE [NEVENTS]}"
CFGBASE="${2:?Usage: run_smoke_job.sh NAME CFGBASE [NEVENTS]}"
NEVENTS="${3:-30}"
PROCID=0

BASEDIR="/afs/cern.ch/user/${USER:0:1}/${USER}/omtf_dataset_production"
JOBDIR="${_CONDOR_SCRATCH_DIR:-$(pwd)}"
cd "${JOBDIR}"

echo "=== SMOKE CONDOR JOB: ${NAME} (config=${CFGBASE}) NEVENTS=${NEVENTS} ==="
echo "Host: $(hostname)   Scratch: ${JOBDIR}"
date

# Pre-create every file/dir listed in the submit file's transfer_output_files
# so HTCondor's output transfer never fails (goes HELD) regardless of which
# branch below actually executes (e.g. cmsRun failure).
mkdir -p "${JOBDIR}/full_campaign_audit"
touch "${JOBDIR}/run_local_test.log" "${JOBDIR}/full_campaign_audit.log" \
      "${JOBDIR}/omtf_hits.root" "${JOBDIR}/omtf_nano.root"

# --- 1. Run the small local cmsRun test (its own /tmp scratch, worker-local) ---
"${BASEDIR}/condor/run_local_test.sh" "${CFGBASE}" "${NEVENTS}" "${PROCID}" \
    > "${JOBDIR}/run_local_test.log" 2>&1
CMSRUN_RC=$?
echo "run_local_test.sh exit code: ${CMSRUN_RC}"

LOCALTEST_SCRATCH="/tmp/${USER}_omtf_localtest_${CFGBASE}_${PROCID}"
HITS="${LOCALTEST_SCRATCH}/omtf_hits_${CFGBASE}_${PROCID}_localtest.root"
NANO="${LOCALTEST_SCRATCH}/omtf_nano_${CFGBASE}_${PROCID}_localtest.root"

AUDIT_STATUS="skipped_cmsrun_failed"
if [[ ${CMSRUN_RC} -eq 0 && -f "${HITS}" && -f "${NANO}" ]]; then
    cp "${HITS}" "${JOBDIR}/omtf_hits.root"
    cp "${NANO}" "${JOBDIR}/omtf_nano.root"

    # --- 2. Re-establish CMSSW environment for the audit step ---
    # (run_local_test.sh set this up in its own subprocess only; it does not
    # persist back to this wrapper, so it must be sourced again here.)
    set +u
    source /cvmfs/cms.cern.ch/cmsset_default.sh
    export SCRAM_ARCH="el9_amd64_gcc12"
    CMSSW_DIR="/afs/cern.ch/user/${USER:0:1}/${USER}/CMSSW_14_2_0_pre2"
    cd "${CMSSW_DIR}/src"
    eval "$(scramv1 runtime -sh)"
    cd "${JOBDIR}"
    set -u

    python3 "${BASEDIR}/scripts/audit/audit_full_campaign.py" \
        --manifest "${BASEDIR}/dataset_manifest.json" \
        --hits "${JOBDIR}/omtf_hits.root" \
        --nano "${JOBDIR}/omtf_nano.root" \
        --outdir "${JOBDIR}/full_campaign_audit" \
        > "${JOBDIR}/full_campaign_audit.log" 2>&1
    if [[ $? -eq 0 ]]; then
        AUDIT_STATUS="pass"
    else
        AUDIT_STATUS="fail"
    fi
else
    echo "cmsRun failed or expected outputs missing -- skipping audit" | tee "${JOBDIR}/full_campaign_audit.log"
fi

rm -rf "${LOCALTEST_SCRATCH}" 2>/dev/null || true

python3 - "${NAME}" "${CFGBASE}" "${CMSRUN_RC}" "${AUDIT_STATUS}" > "${JOBDIR}/smoke_result.json" <<'PYEOF'
import json, sys, datetime
name, cfgbase, cmsrun_rc, audit_status = sys.argv[1:5]
print(json.dumps({
    "name": name,
    "config": cfgbase,
    "cmsrun_exit_code": int(cmsrun_rc),
    "audit_status": audit_status,
    "timestamp": datetime.datetime.now().isoformat(),
}))
PYEOF

echo "=== Smoke job ${NAME} done: cmsrun_rc=${CMSRUN_RC} audit=${AUDIT_STATUS} ==="
date
exit 0
