#!/bin/bash
###############################################################################
# run_das_job_prodv3.sh — HTCondor worker for DAS dataset L1 re-emulation
#
# Usage: run_das_job_prodv3.sh <LABEL> <PROCID> <INPUT_FILE> <GLOBAL_TAG> <SKIP_EVENTS> <MAX_EVENTS>
#
#   LABEL       - validation class label, e.g. minbias, displaced_lowpt, ...
#   PROCID      - job index (for unique output filenames and EOS placement)
#   INPUT_FILE  - /store/mc/... path (without root:// prefix)
#   GLOBAL_TAG  - CMSSW Global Tag, e.g. 140X_mcRun4_realistic_v4
#   SKIP_EVENTS - number of events to skip in input file (chunk start)
#   MAX_EVENTS  - number of events to process in this job (-1 for full file)
#
# Outputs (copied to EOS):
#   ${EOS_BASE}/${LABEL}/omtf_hits_${LABEL}_${PROCID}.root  (TFileService)
#   ${EOS_BASE}/${LABEL}/omtf_nano_${LABEL}_${PROCID}.root  (NanoAOD)
###############################################################################
set -euo pipefail

LABEL="${1:?Usage: run_das_job_prodv3.sh LABEL PROCID INPUT_FILE GLOBAL_TAG SKIP_EVENTS MAX_EVENTS}"
PROCID="${2:?Usage: run_das_job_prodv3.sh LABEL PROCID INPUT_FILE GLOBAL_TAG SKIP_EVENTS MAX_EVENTS}"
INPUT_FILE="${3:?Usage: run_das_job_prodv3.sh LABEL PROCID INPUT_FILE GLOBAL_TAG SKIP_EVENTS MAX_EVENTS}"
GLOBAL_TAG="${4:?Usage: run_das_job_prodv3.sh LABEL PROCID INPUT_FILE GLOBAL_TAG SKIP_EVENTS MAX_EVENTS}"
SKIP_EVENTS="${5:?Usage: run_das_job_prodv3.sh LABEL PROCID INPUT_FILE GLOBAL_TAG SKIP_EVENTS MAX_EVENTS}"
MAX_EVENTS="${6:?Usage: run_das_job_prodv3.sh LABEL PROCID INPUT_FILE GLOBAL_TAG SKIP_EVENTS MAX_EVENTS}"

CMSSW_VERSION="${OMTF_CMSSW_VERSION:-CMSSW_20_0_0_patch2}"
EOS_BASE="${OMTF_EOS_BASE:-/eos/user/${USER:0:1}/${USER}/omtf_hecin_datasets/prodv3_das}"

OMTF_OUTPUT="omtf_hits_${LABEL}_${PROCID}.root"
NANO_OUTPUT="omtf_nano_${LABEL}_${PROCID}.root"
JOB_LOG="job_log_${LABEL}_${PROCID}.txt"

# Redirect all output to a local log file only (uploaded to EOS on exit for
# diagnosis below) -- NOT tee'd to actual stdout/stderr, since HTCondor writes
# those straight to AFS and this console can be verbose; EOS is the intended
# home for the full log, AFS should only ever see this script's own markers.
exec > "/tmp/${JOB_LOG}" 2>&1

cleanup() {
    local EXIT_CODE=$?
    # Upload job log to EOS so failed jobs can be diagnosed
    local LOG_SRC="/tmp/${JOB_LOG}"
    if [ -f "${LOG_SRC}" ]; then
        xrdcp --force "${LOG_SRC}" \
            "root://eosuser.cern.ch/${EOS_BASE}/${LABEL}/logs/${JOB_LOG}" 2>/dev/null || true
    fi
    # Free the big files; keep the rest of the scratch dir (it holds the Kerberos cache HTCondor needs to
    # finish the transfer to EOS). HTCondor removes the scratch dir itself.
    rm -rf "${SCRATCH:-/dev/null}/${CMSSW_VERSION}" "${SCRATCH:-/dev/null}"/*.root 2>/dev/null || true
}
trap cleanup EXIT

echo "=== DAS Re-emulation: ${LABEL} / ProcId ${PROCID} ==="
echo "    Input : ${INPUT_FILE}"
echo "    GT    : ${GLOBAL_TAG}"
echo "    Skip  : ${SKIP_EVENTS}"
echo "    Events: ${MAX_EVENTS}"
date

# --- 1. Scratch working directory (local to the worker, never AFS) ---
SCRATCH="${_CONDOR_SCRATCH_DIR:-$(mktemp -d /tmp/${USER}_omtf_prodv3.XXXXXX)}"
cd "${SCRATCH}"

# --- 2. Unpack CMSSW area + configs (shipped with transfer_input_files from EOS) ---
TARBALL="${SCRATCH}/cmssw_prodv3.tar.gz"
[ -f "${TARBALL}" ] || { echo "ERROR: ${TARBALL} was not transferred"; exit 1; }
tar -xzf "${TARBALL}" && rm -f "${TARBALL}"
CMSSW_DIR="${SCRATCH}/${CMSSW_VERSION}"
CONFDIR="${SCRATCH}/payload/configs_prodv3"

# --- 3. CMSSW environment ---
set +u
source /cvmfs/cms.cern.ch/cmsset_default.sh
set -u
export SCRAM_ARCH="el9_amd64_gcc13"
cd "${CMSSW_DIR}/src"
set +u; scramv1 b ProjectRename >/dev/null 2>&1; eval "$(scramv1 runtime -sh)"; set -u
cd "${SCRATCH}"

cp "${CONFDIR}/das_reemul_cfg.py"        ./job_cfg.py
cp "${CONFDIR}/customize_omtf_dumper.py" ./

# Producer provenance (CMSSW area + config commits) recorded when the tarball was built.
# A "+dirty" suffix means the area had uncommitted changes at that time.
export OMTF_PRODUCER_GIT_COMMIT="$(cat "${SCRATCH}/payload/producer_commit.txt" 2>/dev/null || echo unknown)"

# --- 3. Export env vars consumed by das_reemul_cfg.py ---
export DAS_INPUT_FILE="${INPUT_FILE}"
export DAS_GLOBAL_TAG="${GLOBAL_TAG}"
export DAS_OMTF_OUTPUT="${OMTF_OUTPUT}"
export DAS_NANO_OUTPUT="${NANO_OUTPUT}"
export DAS_SKIP_EVENTS="${SKIP_EVENTS}"
export DAS_MAX_EVENTS="${MAX_EVENTS}"

# --- 4. Run cmsRun (retry on transient XRootD errors) ---
MAX_ATTEMPTS=3
ATTEMPT=0
RC=0
while true; do
    ATTEMPT=$(( ATTEMPT + 1 ))
    echo "Starting cmsRun (attempt ${ATTEMPT}/${MAX_ATTEMPTS})..."
    date
    rm -f "${OMTF_OUTPUT}" "${NANO_OUTPUT}"
    RC=0; cmsRun job_cfg.py || RC=$?
    echo "cmsRun finished with exit code ${RC}"
    date
    if [ ${RC} -eq 0 ]; then
        break
    fi
    if [ ${ATTEMPT} -ge ${MAX_ATTEMPTS} ]; then
        echo "ERROR: cmsRun failed for ${LABEL}/${PROCID} after ${MAX_ATTEMPTS} attempts (last RC=${RC})"
        exit ${RC}
    fi
    if [ ${RC} -eq 84 ] || [ ${RC} -eq 85 ] || [ ${RC} -eq 92 ]; then
        echo "WARNING: Transient XRootD/file-open error (${RC}), retrying in 5 minutes..."
        sleep 300
    else
        echo "ERROR: cmsRun failed with non-transient RC=${RC}, not retrying"
        exit ${RC}
    fi
done

# --- 5. Copy outputs to EOS ---
EOS_DIR="${EOS_BASE}/${LABEL}"
eos mkdir -p "${EOS_DIR}" 2>/dev/null || true

if [ ! -f "${OMTF_OUTPUT}" ]; then
    echo "ERROR: OMTF output ${OMTF_OUTPUT} not found"
    ls -la
    exit 1
fi

echo "Copying ${OMTF_OUTPUT} ($(du -sh "${OMTF_OUTPUT}" | cut -f1)) → ${EOS_DIR}/"
xrdcp --force "${OMTF_OUTPUT}" "root://eosuser.cern.ch/${EOS_DIR}/${OMTF_OUTPUT}"
echo "Upload complete"

if [ -f "${NANO_OUTPUT}" ]; then
    echo "Copying ${NANO_OUTPUT} ($(du -sh "${NANO_OUTPUT}" | cut -f1)) → ${EOS_DIR}/"
    xrdcp --force "${NANO_OUTPUT}" "root://eosuser.cern.ch/${EOS_DIR}/${NANO_OUTPUT}"
    echo "Upload complete"
else
    echo "WARNING: NanoAOD output ${NANO_OUTPUT} not found — skipping"
fi

echo "=== DAS job ${LABEL}/${PROCID} complete ==="
date
