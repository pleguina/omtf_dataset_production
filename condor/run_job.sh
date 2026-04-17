#!/bin/bash
###############################################################################
# run_job.sh — HTCondor worker for OMTF HECIN production
#
# Usage:  run_job.sh  <DATASET>  <PROCID>  <SKIPEVENTS>  <NEVENTS>
#
# Note: SKIPEVENTS is accepted for submit-file compatibility but intentionally
# unused for EmptySource generation; each job is statistically independent.
###############################################################################
set -euo pipefail

DATASET="${1:?Usage: run_job.sh DATASET PROCID SKIPEVENTS NEVENTS}"
PROCID="${2:?Usage: run_job.sh DATASET PROCID SKIPEVENTS NEVENTS}"
SKIPEVENTS="${3:?Usage: run_job.sh DATASET PROCID SKIPEVENTS NEVENTS}"
NEVENTS="${4:?Usage: run_job.sh DATASET PROCID SKIPEVENTS NEVENTS}"

BASEDIR="/afs/cern.ch/user/${USER:0:1}/${USER}/omtf_hecin_dataset_production"
CMSSW_VERSION="CMSSW_14_2_0_pre2"
CMSSW_DIR="/afs/cern.ch/user/${USER:0:1}/${USER}/${CMSSW_VERSION}"
CONFDIR="${BASEDIR}/configs"
EOS_BASE="/eos/user/${USER:0:1}/${USER}/omtf_hecin_datasets/prod"

OMTF_OUTPUT="omtf_hits_${DATASET}_${PROCID}.root"
NANO_OUTPUT="omtf_nano_${DATASET}_${PROCID}.root"
POOL_OUTFILE="${DATASET}_${PROCID}.root"
SEED=$((PROCID * 7919 + 13))

# Guaranteed cleanup — runs on any exit (success, failure, or signal)
trap 'rm -f "${SCRATCH:-/dev/null}"/*.root "${SCRATCH:-/dev/null}"/*.py 2>/dev/null || true' EXIT

echo "=== OMTF HECIN Production: ${DATASET} / ProcId ${PROCID} / ${NEVENTS} events (skip=${SKIPEVENTS}) ==="
date

# --- 1. CMSSW environment ---
set +u
source /cvmfs/cms.cern.ch/cmsset_default.sh
set -u
export SCRAM_ARCH="el9_amd64_gcc12"
cd "${CMSSW_DIR}/src"
set +u; eval "$(scramv1 runtime -sh)"; set -u

# --- 2. Scratch working directory ---
SCRATCH="${_CONDOR_SCRATCH_DIR:-/tmp/${USER}_omtf_prod_${DATASET}_${PROCID}}"
mkdir -p "${SCRATCH}"
cd "${SCRATCH}"

# Copy config + customizer
cp "${CONFDIR}/${DATASET}_cfg.py"        ./job_cfg.py
cp "${CONFDIR}/customize_omtf_dumper.py" ./

# --- 3. Patch config via Python (robust, env-driven) ---
export PATCH_NEVENTS="${NEVENTS}"
export PATCH_POOL_OUTFILE="${POOL_OUTFILE}"
export PATCH_OMTF_OUTPUT="${OMTF_OUTPUT}"
export PATCH_NANO_OUTPUT="${NANO_OUTPUT}"
export PATCH_SEED="${SEED}"
export PATCH_DATASET="${DATASET}"
export PATCH_PROCID="${PROCID}"

python3 - << 'PYEOF'
import re, os

nevents = int(os.environ["PATCH_NEVENTS"])
pool_out = os.environ["PATCH_POOL_OUTFILE"]
omtf_out = os.environ["PATCH_OMTF_OUTPUT"]
nano_out = os.environ["PATCH_NANO_OUTPUT"]
seed = int(os.environ["PATCH_SEED"])
dataset = os.environ["PATCH_DATASET"]
procid = os.environ["PATCH_PROCID"]

with open("job_cfg.py") as f:
    cfg = f.read()

# Override maxEvents — patch only input= field
cfg = re.sub(
    r"(process\.maxEvents\s*=\s*cms\.untracked\.PSet\([^)]*input\s*=\s*)cms\.untracked\.int32\(\d+\)",
    rf"\g<1>cms.untracked.int32({nevents})",
    cfg,
)

# Force single-thread/single-stream to avoid ROOT/TFileService races
cfg = cfg.replace(
    "process.options.numberOfThreads = 4",
    "process.options.numberOfThreads = 1",
)
cfg = cfg.replace(
    "process.options.numberOfStreams = 0",
    "process.options.numberOfStreams = 1",
)

# Override pool output filename
cfg = cfg.replace(f"file:{dataset}.root", f"file:{pool_out}")

# Append per-job seed + OMTF output override + NanoAOD output override
cfg += f"""
# === Production per-job overrides ===
if not hasattr(process, 'RandomNumberGeneratorService'):
    process.RandomNumberGeneratorService = cms.Service('RandomNumberGeneratorService')
process.RandomNumberGeneratorService.generator = cms.PSet(
    initialSeed = cms.untracked.uint32({seed}),
)
if hasattr(process.RandomNumberGeneratorService, 'VtxSmeared'):
    process.RandomNumberGeneratorService.VtxSmeared.initialSeed = cms.untracked.uint32({seed + 1000000})
if hasattr(process.RandomNumberGeneratorService, 'g4SimHits'):
    process.RandomNumberGeneratorService.g4SimHits.initialSeed = cms.untracked.uint32({seed + 2000000})
process.TFileService.fileName = cms.string('{omtf_out}')
if hasattr(process, 'NANOOMTFoutput'):
    process.NANOOMTFoutput.fileName = cms.untracked.string('{nano_out}')
"""

with open("job_cfg.py", "w") as f:
    f.write(cfg)

print(f"Config patched: {dataset}/{procid}, {nevents} events, OMTF -> {omtf_out}, NanoAOD -> {nano_out}")
PYEOF

# --- 4. Run cmsRun (with retry for transient XRootD/EOS network failures) ---
# Exit codes 85 (XRootD connection error) and 92 (network unreachable) are
# transient EOS issues — retry up to 3 times with a 5-minute cooldown.
MAX_ATTEMPTS=3
ATTEMPT=0
RC=0
while true; do
    ATTEMPT=$(( ATTEMPT + 1 ))
    echo "Starting cmsRun (attempt ${ATTEMPT}/${MAX_ATTEMPTS})..."
    date
    # Remove any partial output from a previous attempt before retrying
    rm -f "${OMTF_OUTPUT}" "${POOL_OUTFILE}"
    RC=0; cmsRun job_cfg.py || RC=$?
    echo "cmsRun finished with exit code ${RC}"
    date
    if [ ${RC} -eq 0 ]; then
        break
    fi
    if [ ${ATTEMPT} -ge ${MAX_ATTEMPTS} ]; then
        echo "ERROR: cmsRun failed for ${DATASET}/${PROCID} after ${MAX_ATTEMPTS} attempts (last exit code ${RC})"
        exit ${RC}
    fi
    # Only retry on known transient XRootD/EOS network error codes
    if [ ${RC} -eq 85 ] || [ ${RC} -eq 92 ]; then
        echo "WARNING: Transient network error (${RC}), retrying in 5 minutes..."
        sleep 300
    else
        echo "ERROR: cmsRun failed with non-transient exit code ${RC}, not retrying"
        exit ${RC}
    fi
done

# --- 5. Copy outputs to EOS ---
EOS_DIR="${EOS_BASE}/${DATASET}"

if [ ! -f "${OMTF_OUTPUT}" ]; then
    echo "ERROR: OMTF output ${OMTF_OUTPUT} not found after cmsRun"
    ls -la
    exit 1
fi

eos mkdir -p "${EOS_DIR}" 2>/dev/null || true

echo "Copying ${OMTF_OUTPUT} ($(du -sh "${OMTF_OUTPUT}" | cut -f1)) to EOS: ${EOS_DIR}/"
xrdcp --force "${OMTF_OUTPUT}" "root://eosuser.cern.ch/${EOS_DIR}/${OMTF_OUTPUT}"
echo "Upload complete: ${EOS_DIR}/${OMTF_OUTPUT}"

# Copy NanoAOD output if produced
if [ -f "${NANO_OUTPUT}" ]; then
    echo "Copying ${NANO_OUTPUT} ($(du -sh "${NANO_OUTPUT}" | cut -f1)) to EOS: ${EOS_DIR}/"
    xrdcp --force "${NANO_OUTPUT}" "root://eosuser.cern.ch/${EOS_DIR}/${NANO_OUTPUT}"
    echo "Upload complete: ${EOS_DIR}/${NANO_OUTPUT}"
else
    echo "WARNING: NanoAOD output ${NANO_OUTPUT} not found — skipping (check if L1Trigger/L1MuNano is compiled)"
fi

# Optional RAWSIM copy disabled by default (very large)
# if [ -f "${POOL_OUTFILE}" ]; then
#     xrdcp --force "${POOL_OUTFILE}" "root://eosuser.cern.ch/${EOS_DIR}/${POOL_OUTFILE}"
# fi

echo "=== Production job ${DATASET}/${PROCID} complete ==="
date
