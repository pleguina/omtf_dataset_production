#!/bin/bash
###############################################################################
# run_job_prodv3.sh — HTCondor worker for OMTF production to prodv3 EOS (CMSSW_20_0_0_patch2, new OMTF)
#
# Usage:  run_job_prodv3.sh  <DATASET>  <PROCID>  <SKIPEVENTS>  <NEVENTS> [PARTITION_ID]
#
# Note: SKIPEVENTS is accepted for submit-file compatibility but intentionally
# unused for EmptySource generation; each job is statistically independent.
###############################################################################
set -euo pipefail

DATASET="${1:?Usage: run_job_prodv3.sh DATASET PROCID SKIPEVENTS NEVENTS}"
PROCID="${2:?Usage: run_job_prodv3.sh DATASET PROCID SKIPEVENTS NEVENTS}"
SKIPEVENTS="${3:?Usage: run_job_prodv3.sh DATASET PROCID SKIPEVENTS NEVENTS}"
NEVENTS="${4:?Usage: run_job_prodv3.sh DATASET PROCID SKIPEVENTS NEVENTS}"
# Optional partition namespace (defaults to env OMTF_PARTITION_ID or 0)
PARTITION_ID="${5:-${OMTF_PARTITION_ID:-0}}"

BASEDIR="/afs/cern.ch/user/${USER:0:1}/${USER}/omtf_dataset_production"
CMSSW_VERSION="${OMTF_CMSSW_VERSION:-CMSSW_20_0_0_patch2}"
CMSSW_DIR="/afs/cern.ch/user/${USER:0:1}/${USER}/${CMSSW_VERSION}"
CONFDIR="${BASEDIR}/configs_prodv3"
EOS_BASE="${OMTF_EOS_BASE:-/eos/user/${USER:0:1}/${USER}/omtf_hecin_datasets/prodv3}"

OMTF_OUTPUT="omtf_hits_${DATASET}_${PROCID}.root"
NANO_OUTPUT="omtf_nano_${DATASET}_${PROCID}.root"
POOL_OUTFILE="${DATASET}_${PROCID}.root"

# Campaign namespace for deterministic RNG/event identity.
# Priority: explicit OMTF_CAMPAIGN_ID > Condor cluster id > fallback 1.
CAMPAIGN_ID="${OMTF_CAMPAIGN_ID:-${_CONDOR_CLUSTER_ID:-1}}"

# Guaranteed cleanup — runs on any exit (success, failure, or signal)
trap 'rm -f "${SCRATCH:-/dev/null}"/*.root "${SCRATCH:-/dev/null}"/*.py 2>/dev/null || true' EXIT

echo "=== OMTF Production v3: ${DATASET} / ProcId ${PROCID} / ${NEVENTS} events (skip=${SKIPEVENTS}) ==="
date

# --- 1. CMSSW environment ---
set +u
source /cvmfs/cms.cern.ch/cmsset_default.sh
set -u
export SCRAM_ARCH="el9_amd64_gcc13"
cd "${CMSSW_DIR}/src"
set +u; eval "$(scramv1 runtime -sh)"; set -u

# --- 2. Scratch working directory ---
SCRATCH="${_CONDOR_SCRATCH_DIR:-/tmp/${USER}_omtf_prodv3_${DATASET}_${PROCID}}"
mkdir -p "${SCRATCH}"
cd "${SCRATCH}"

# Copy config + customizer
cp "${CONFDIR}/${DATASET}_cfg.py"        ./job_cfg.py
cp "${CONFDIR}/customize_omtf_dumper.py" ./

# Record the producer (CMSSW area) and config commits that generated this file (schema §1 provenance).
# A "+dirty" suffix means the area had uncommitted changes when the job ran.
_git_id() { local h; h="$(git -C "$1" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)";
            [ -n "$(git -C "$1" status --porcelain --untracked-files=no 2>/dev/null)" ] && h="${h}+dirty"; echo "$h"; }
export OMTF_PRODUCER_GIT_COMMIT="cmssw:$(_git_id "${CMSSW_DIR}/src");prod:$(_git_id "${BASEDIR}")"

# --- 3. Patch config via Python (robust, env-driven) ---
export PATCH_NEVENTS="${NEVENTS}"
export PATCH_POOL_OUTFILE="${POOL_OUTFILE}"
export PATCH_OMTF_OUTPUT="${OMTF_OUTPUT}"
export PATCH_NANO_OUTPUT="${NANO_OUTPUT}"
export PATCH_DATASET="${DATASET}"
export PATCH_PROCID="${PROCID}"
export PATCH_JOBID="${PROCID}"
export PATCH_PARTITION_ID="${PARTITION_ID}"
export PATCH_CAMPAIGN_ID="${CAMPAIGN_ID}"

python3 - << 'PYEOF'
import os
import re
import zlib

nevents = int(os.environ["PATCH_NEVENTS"])
pool_out = os.environ["PATCH_POOL_OUTFILE"]
omtf_out = os.environ["PATCH_OMTF_OUTPUT"]
nano_out = os.environ["PATCH_NANO_OUTPUT"]
dataset = os.environ["PATCH_DATASET"]
procid = os.environ["PATCH_PROCID"]
job_id = int(os.environ["PATCH_JOBID"])
partition_id = int(os.environ["PATCH_PARTITION_ID"])
campaign_id = int(os.environ["PATCH_CAMPAIGN_ID"])

# Condor cluster id can be zero/empty in local runs; enforce valid run number.
if campaign_id <= 0:
    campaign_id = 1

# Global job index inside a campaign: partitions are 500 jobs each for the
# 1M/3M B4 layouts, so folding the partition into the job index guarantees
# unique seeds and unique event namespaces across all partitions.
global_job_id = partition_id * 500 + job_id

def seed_for(stream_name: str) -> int:
    key = f"{campaign_id}:{dataset}:{partition_id}:{global_job_id}:{stream_name}"
    return 1 + ((zlib.crc32(key.encode("utf-8")) & 0xFFFFFFFF) % 900_000_000)

seed_generator = seed_for("generator")
seed_mix = seed_for("mix")
seed_vtx = seed_for("VtxSmeared")
seed_g4 = seed_for("g4SimHits")

first_run = campaign_id
first_lumi = global_job_id + 1  # differs per job
first_event = global_job_id * nevents + 1

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
    initialSeed = cms.untracked.uint32({seed_generator}),
)
# Critical for PU datasets (especially B4): unique pileup-mixing RNG per ProcId.
if hasattr(process, 'mix'):
    process.RandomNumberGeneratorService.mix = cms.PSet(
        initialSeed = cms.untracked.uint32({seed_mix}),
    )
if hasattr(process.RandomNumberGeneratorService, 'VtxSmeared'):
    process.RandomNumberGeneratorService.VtxSmeared.initialSeed = cms.untracked.uint32({seed_vtx})
if hasattr(process.RandomNumberGeneratorService, 'g4SimHits'):
    process.RandomNumberGeneratorService.g4SimHits.initialSeed = cms.untracked.uint32({seed_g4})
# Every other engine (digitizers, mixing sub-engines, ...) gets its own deterministic seed too.
import zlib as _zlib
_seeded = {{'generator', 'mix', 'VtxSmeared', 'g4SimHits'}}
for _name in process.RandomNumberGeneratorService.parameterNames_():
    _p = getattr(process.RandomNumberGeneratorService, _name)
    if _name in _seeded or not isinstance(_p, cms.PSet) or not hasattr(_p, 'initialSeed'):
        continue
    _key = "{campaign_id}:{dataset}:{partition_id}:{global_job_id}:" + _name
    _p.initialSeed = cms.untracked.uint32(1 + (_zlib.crc32(_key.encode()) & 0xFFFFFFFF) % 900_000_000)

# Global event-identity namespace per campaign/partition/job.
# Guarantees (run, lumi, event) uniqueness across jobs within a campaign.
if hasattr(process, 'source'):
    process.source.firstRun = cms.untracked.uint32({first_run})
    process.source.firstLuminosityBlock = cms.untracked.uint32({first_lumi})
    process.source.firstEvent = cms.untracked.uint64({first_event})

# B4 is PU-only (neutrino gun). Rotate MinBias source list by ProcId so jobs
# do not keep starting from the same file ordering.
_job_dataset = {dataset!r}
_job_procid = {job_id}
_job_global_id = {partition_id} * 500 + _job_procid
if _job_dataset == 'B4' and hasattr(process, 'mix') and hasattr(process.mix, 'input') and hasattr(process.mix.input, 'fileNames'):
    mb = list(process.mix.input.fileNames)
    if len(mb) > 1:
        offset = _job_global_id % len(mb)
        process.mix.input.fileNames = cms.untracked.vstring(mb[offset:] + mb[:offset])

process.TFileService.fileName = cms.string('{omtf_out}')
if hasattr(process, 'NANOOMTFoutput'):
    process.NANOOMTFoutput.fileName = cms.untracked.string('{nano_out}')
"""

with open("job_cfg.py", "w") as f:
    f.write(cfg)

print(f"Config patched: {dataset}/{procid}, {nevents} events, OMTF -> {omtf_out}, NanoAOD -> {nano_out}")
print(f"Seed vector: generator={seed_generator}, mix={seed_mix}, VtxSmeared={seed_vtx}, g4SimHits={seed_g4}")
print(f"Event namespace: run={first_run}, lumi={first_lumi}, firstEvent={first_event}, globalJobId={global_job_id}")
PYEOF

# --- 4. Run cmsRun (with retry for transient XRootD/EOS network failures) ---
# NOTE: cmsRun's console output (framework %MSG warnings, per-event xrootd
# open/close chatter, etc.) is redirected to a LOCAL scratch file, never to
# stdout/stderr directly -- those get written straight to AFS by HTCondor
# (output=/error=) and a single PU200 job's raw console output can be 1-2MB,
# which multiplied across ~13k jobs would blow the AFS quota. Only a bounded
# summary (and, on failure, a tail of the real log) is ever emitted here.
CMSRUN_LOG="${SCRATCH}/cmsrun_console.log"

MAX_ATTEMPTS=3
ATTEMPT=0
RC=0
while true; do
    ATTEMPT=$(( ATTEMPT + 1 ))
    echo "Starting cmsRun (attempt ${ATTEMPT}/${MAX_ATTEMPTS})..."
    date
    rm -f "${OMTF_OUTPUT}" "${POOL_OUTFILE}"
    RC=0; cmsRun job_cfg.py > "${CMSRUN_LOG}" 2>&1 || RC=$?
    echo "cmsRun finished with exit code ${RC}"
    date
    if [ ${RC} -ne 0 ]; then
        echo "----- last 200 lines of cmsRun console (full log stays on worker scratch only) -----"
        tail -n 200 "${CMSRUN_LOG}" 2>/dev/null || true
        echo "----- end of cmsRun console tail -----"
    fi
    if [ ${RC} -eq 0 ]; then
        break
    fi
    if [ ${ATTEMPT} -ge ${MAX_ATTEMPTS} ]; then
        echo "ERROR: cmsRun failed for ${DATASET}/${PROCID} after ${MAX_ATTEMPTS} attempts (last exit code ${RC})"
        exit ${RC}
    fi
    if [ ${RC} -eq 85 ] || [ ${RC} -eq 92 ]; then
        echo "WARNING: Transient network error (${RC}), retrying in 5 minutes..."
        sleep 300
    else
        echo "ERROR: cmsRun failed with non-transient exit code ${RC}, not retrying"
        exit ${RC}
    fi
done

# --- 5. Copy outputs to EOS prodv3 ---
EOS_DIR="${EOS_BASE}/${DATASET}"

if [ ! -f "${OMTF_OUTPUT}" ]; then
    echo "ERROR: OMTF output ${OMTF_OUTPUT} not found after cmsRun"
    ls -la
    exit 1
fi

eos mkdir -p "${EOS_DIR}" 2>/dev/null || true

echo "Copying ${OMTF_OUTPUT} ($(du -sh "${OMTF_OUTPUT}" | cut -f1)) to EOS prodv3: ${EOS_DIR}/"
xrdcp --force "${OMTF_OUTPUT}" "root://eosuser.cern.ch/${EOS_DIR}/${OMTF_OUTPUT}"
echo "Upload complete: ${EOS_DIR}/${OMTF_OUTPUT}"

if [ -f "${NANO_OUTPUT}" ]; then
    echo "Copying ${NANO_OUTPUT} ($(du -sh "${NANO_OUTPUT}" | cut -f1)) to EOS prodv3: ${EOS_DIR}/"
    xrdcp --force "${NANO_OUTPUT}" "root://eosuser.cern.ch/${EOS_DIR}/${NANO_OUTPUT}"
    echo "Upload complete: ${EOS_DIR}/${NANO_OUTPUT}"
else
    echo "WARNING: NanoAOD output ${NANO_OUTPUT} not found — skipping"
fi

echo "=== Production v3 job ${DATASET}/${PROCID} complete ==="
date
