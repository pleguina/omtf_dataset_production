#!/bin/bash
###############################################################################
# run_local_test.sh — run a small local cmsRun test for B datasets
#
# Usage:  ./run_local_test.sh  <DATASET>  [NEVENTS]  [PROCID]
# Example: ./run_local_test.sh B1 10 0
###############################################################################
set -euo pipefail

DATASET="${1:?Usage: run_local_test.sh DATASET [NEVENTS] [PROCID]}"
NEVENTS="${2:-10}"
PROCID="${3:-0}"

BASEDIR="/afs/cern.ch/user/${USER:0:1}/${USER}/omtf_hecin_dataset_production"
CMSSW_VERSION="CMSSW_14_2_0_pre2"
CMSSW_DIR="/afs/cern.ch/user/${USER:0:1}/${USER}/${CMSSW_VERSION}"
CONFDIR="${BASEDIR}/configs"

OMTF_OUTPUT="omtf_hits_${DATASET}_${PROCID}_localtest.root"
POOL_OUTFILE="${DATASET}_${PROCID}_localtest.root"
SEED=$((PROCID * 7919 + 13))

SCRATCH="/tmp/${USER}_omtf_localtest_${DATASET}_${PROCID}"

echo "=== LOCAL TEST: ${DATASET} / ProcId ${PROCID} / ${NEVENTS} events ==="
echo "    Scratch: ${SCRATCH}"
date

# --- 1. CMSSW environment ---
set +u
source /cvmfs/cms.cern.ch/cmsset_default.sh
set -u
export SCRAM_ARCH="el9_amd64_gcc12"
cd "${CMSSW_DIR}/src"
set +u; eval "$(scramv1 runtime -sh)"; set -u

# --- 2. Scratch working directory ---
mkdir -p "${SCRATCH}"
cd "${SCRATCH}"

# Copy config + customizer
cp "${CONFDIR}/${DATASET}_cfg.py"        ./job_cfg.py
cp "${CONFDIR}/customize_omtf_dumper.py" ./

# --- 3. Patch config ---
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

# Override maxEvents
cfg = re.sub(
    r"(process\.maxEvents\s*=\s*cms\.untracked\.PSet\([^)]*input\s*=\s*)cms\.untracked\.int32\(\d+\)",
    rf"\g<1>cms.untracked.int32({nevents})",
    cfg,
)

# Force single-thread/single-stream
cfg = cfg.replace("process.options.numberOfThreads = 4", "process.options.numberOfThreads = 1")
cfg = cfg.replace("process.options.numberOfStreams = 0", "process.options.numberOfStreams = 1")

# Override pool output filename
cfg = cfg.replace(f"file:{dataset}.root", f"file:{pool_out}")

# Append per-job overrides
cfg += f"""
# === Local test overrides ===
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

print(f"Config patched: {dataset}/{procid}, {nevents} events -> {omtf_out}")
PYEOF

# --- 4. Run cmsRun ---
echo "Starting cmsRun..."
date
cmsRun job_cfg.py 2>&1 | tee cmsrun_${DATASET}_${PROCID}.log
RC=${PIPESTATUS[0]}
echo "cmsRun finished with exit code ${RC}"
date

if [ ${RC} -ne 0 ]; then
    echo "ERROR: cmsRun failed for ${DATASET}/${PROCID}"
    echo "Last 50 lines of log:"
    tail -50 cmsrun_${DATASET}_${PROCID}.log
    echo "Log saved to: ${SCRATCH}/cmsrun_${DATASET}_${PROCID}.log"
    exit ${RC}
fi

echo "SUCCESS. OMTF output: ${SCRATCH}/${OMTF_OUTPUT}"
echo "Log: ${SCRATCH}/cmsrun_${DATASET}_${PROCID}.log"
ls -lh "${SCRATCH}"/*.root 2>/dev/null || true
