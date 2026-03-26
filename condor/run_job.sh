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
POOL_OUTFILE="${DATASET}_${PROCID}.root"
SEED=$((PROCID * 7919 + 13))

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
export PATCH_SEED="${SEED}"
export PATCH_DATASET="${DATASET}"
export PATCH_PROCID="${PROCID}"

python3 - << 'PYEOF'
import re, os

nevents = int(os.environ["PATCH_NEVENTS"])
pool_out = os.environ["PATCH_POOL_OUTFILE"]
omtf_out = os.environ["PATCH_OMTF_OUTPUT"]
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

# Append per-job seed + OMTF output override
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
"""

with open("job_cfg.py", "w") as f:
    f.write(cfg)

print(f"Config patched: {dataset}/{procid}, {nevents} events, OMTF -> {omtf_out}")
PYEOF

# --- 4. Run cmsRun ---
echo "Starting cmsRun..."
date
cmsRun job_cfg.py
RC=$?
echo "cmsRun finished with exit code ${RC}"
date

if [ ${RC} -ne 0 ]; then
    echo "ERROR: cmsRun failed for ${DATASET}/${PROCID}"
    exit ${RC}
fi

# --- 5. Copy OMTF output to EOS ---
EOS_DIR="${EOS_BASE}/${DATASET}"

if [ ! -f "${OMTF_OUTPUT}" ]; then
    echo "ERROR: OMTF output ${OMTF_OUTPUT} not found after cmsRun"
    ls -la
    exit 1
fi

echo "Copying ${OMTF_OUTPUT} ($(du -sh "${OMTF_OUTPUT}" | cut -f1)) to EOS: ${EOS_DIR}/"
eos mkdir -p "${EOS_DIR}" 2>/dev/null || true
xrdcp --force "${OMTF_OUTPUT}" "root://eosuser.cern.ch/${EOS_DIR}/${OMTF_OUTPUT}"
echo "Upload complete: ${EOS_DIR}/${OMTF_OUTPUT}"

# Optional RAWSIM copy disabled by default (very large)
# if [ -f "${POOL_OUTFILE}" ]; then
#     xrdcp --force "${POOL_OUTFILE}" "root://eosuser.cern.ch/${EOS_DIR}/${POOL_OUTFILE}"
# fi

rm -f "${SCRATCH}"/*.root "${SCRATCH}"/*.py 2>/dev/null || true
echo "=== Production job ${DATASET}/${PROCID} complete ==="
date
