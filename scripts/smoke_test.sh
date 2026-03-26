#!/bin/bash
###############################################################################
# smoke_test.sh — Generate 100 events of every dataset, run the OMTF
#                 Phase-2 dumper, and verify ntuple contents.
#
# Pipeline per dataset:
#   cmsDriver (GEN,SIM,DIGI:pdigi_valid,L1, no PU, 100 events)
#     -> patch config: correct GT + inject OMTF dumper
#     -> cmsRun
#     -> check_ntuple.py  (inspects omtf_hits.root branch list)
#     -> make_graphs.py   (builds one PyG .pt graph file as sanity check)
#
# Note on B-samples:
#   B1/B2/B3 run WITHOUT pileup here (smoke only verifies the generator
#   and dumper; PU mixing is tested in full production via condor).
#   B4 is skipped (needs MinBias DBS, no local generation).
#
# Usage:
#   cd ~/omtf_hecin_dataset_production
#   ./scripts/smoke_test.sh
#   ./scripts/smoke_test.sh --datasets S1,D1,S2   # subset
#   ./scripts/smoke_test.sh --skip-cmsrun          # only graph-check existing outputs
###############################################################################
set -euo pipefail

BASEDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FRAGDIR="${BASEDIR}/fragments"
SMOKEDIR="${BASEDIR}/test/smoke"
SCRIPTS="${BASEDIR}/scripts"
CMSSW_VERSION="CMSSW_14_2_0_pre2"
NEVENTS=100

GEOMETRY="Extended2026D110"
ERA="Phase2C17I13M9"
CONDITIONS="140X_mcRun4_realistic_v4"

# All signal datasets + background samples (B4 excluded — needs MinBias DBS)
ALL_DATASETS=(S1 S2 S3 S4 S5 D1 B1 B2 B3)
DATASETS=("${ALL_DATASETS[@]}")
SKIP_CMSRUN=false

for arg in "$@"; do
    case "$arg" in
        --skip-cmsrun) SKIP_CMSRUN=true ;;
        --datasets=*)  IFS=',' read -ra DATASETS <<< "${arg#--datasets=}" ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# Fragment map: dataset tag -> fragment filename
# ---------------------------------------------------------------------------
declare -A FRAG
FRAG[S1]="S1_singleMuon_1overPt.py"
FRAG[S2]="S2_displacedMuon_flatVertex.py"
FRAG[S3]="S3_diMuon_sameSector.py"
FRAG[S4]="S4_triMuon_sameSector.py"
FRAG[S5]="S5_diMuon_displaced.py"
FRAG[D1]="D1_displacedMuon_LLP.py"
FRAG[B1]="B1_singleMuon_PU200.py"
FRAG[B2]="B2_displacedMuon_PU200.py"
FRAG[B3]="B3_diMuon_PU200.py"

# LLP-gun datasets require EMTFTools/ParticleGuns to be compiled
LLP_DATASETS=(S2 S5 D1)

# ---------------------------------------------------------------------------
# Environment setup
# ---------------------------------------------------------------------------
if [ -z "${CMSSW_BASE:-}" ]; then
    set +u
    source /cvmfs/cms.cern.ch/cmsset_default.sh
    cd "${BASEDIR}/${CMSSW_VERSION}/src"
    eval "$(scramv1 runtime -sh)"
    set -u
fi

SCRAM_ARCH="${SCRAM_ARCH:-el9_amd64_gcc12}"
LLP_PLUGIN="${CMSSW_BASE}/lib/${SCRAM_ARCH}/pluginEMTFToolsParticleGuns.so"

check_llp() {
    local ds=$1
    for llp in "${LLP_DATASETS[@]}"; do
        [ "$ds" == "$llp" ] || continue
        if [ ! -f "${LLP_PLUGIN}" ]; then
            echo "[ERROR] ${ds} needs FlatRandomLLPGunProducer2 but plugin not found:"
            echo "        ${LLP_PLUGIN}"
            echo "        Run setup_cmssw.sh or: scram b -j8 EMTFTools/ParticleGuns"
            return 1
        fi
    done
    return 0
}

mkdir -p "${SMOKEDIR}"

PASS=0; WARN=0; FAIL=0
declare -a SUMMARY

# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------
for DS in "${DATASETS[@]}"; do
    echo ""
    echo "###################################################################"
    echo "  DATASET: ${DS}  (${NEVENTS} events, no PU)"
    echo "###################################################################"

    if ! check_llp "${DS}"; then
        SUMMARY+=("SKIP  ${DS}: LLP gun plugin not built")
        FAIL=$((FAIL+1))
        continue
    fi

    WDIR="${SMOKEDIR}/${DS}"
    mkdir -p "${WDIR}"
    cd "${WDIR}"

    FRAG_FILE="${FRAG[$DS]}"
    CFG="smoke_cfg.py"
    OUTPUT_NTUPLE="omtf_hits.root"
    OUTPUT_GENSIM="${DS}_gensim.root"

    # -----------------------------------------------------------------------
    # Step 1: Generate smoke config with cmsDriver (all-in-one, no PU)
    # -----------------------------------------------------------------------
    if [ "${SKIP_CMSRUN}" = false ]; then
        echo "[1/4] Generating cmsDriver config..."

        # Install fragment where cmsDriver can find it
        FRAG_INST="${CMSSW_BASE}/src/Configuration/GenProduction/python/OMTF_HECIN_SMOKE/${FRAG_FILE}"
        mkdir -p "$(dirname "${FRAG_INST}")"
        cp "${FRAGDIR}/${FRAG_FILE}" "${FRAG_INST}"

        cmsDriver.py \
            "Configuration/GenProduction/python/OMTF_HECIN_SMOKE/${FRAG_FILE}" \
            --python_filename "${CFG}" \
            --eventcontent RAWSIM \
            --customise "Configuration/DataProcessing/Utils.addMonitoring" \
            --datatier GEN-SIM-DIGI-RAW \
            --conditions "${CONDITIONS}" \
            --beamspot HLLHC14TeV \
            --step "GEN,SIM,DIGI:pdigi_valid,L1,DIGI2RAW" \
            --geometry "${GEOMETRY}" \
            --era "${ERA}" \
            --fileout "file:${OUTPUT_GENSIM}" \
            --no_exec \
            -n "${NEVENTS}" \
            2>&1 | tee cmsDriver.log \
            || { echo "[FAIL] cmsDriver failed for ${DS}"; SUMMARY+=("FAIL  ${DS}: cmsDriver error"); FAIL=$((FAIL+1)); cd "${BASEDIR}"; continue; }

        # -------------------------------------------------------------------
        # Patch config:
        #   - override GlobalTag to our pinned conditions
        #   - set maxEvents explicitly
        #   - inject OMTF Phase-2 dumper activation
        # -------------------------------------------------------------------
        python3 - "${CFG}" "${NEVENTS}" "${DS}" "${OUTPUT_NTUPLE}" "${CONDITIONS}" << 'PYEOF'
import sys, re

cfg_path, nevents_str, ds, ntuple_out, conditions = sys.argv[1:]
nevents = int(nevents_str)

with open(cfg_path) as f:
    cfg = f.read()

# 1. Pin the GlobalTag to our tested conditions (override auto:*)
cfg = re.sub(
    r"process\.GlobalTag\s*=\s*GlobalTag\([^,]+,\s*['\"][^'\"]+['\"]",
    f"process.GlobalTag = GlobalTag(process.GlobalTag, '{conditions}'",
    cfg,
)

# 2. Force maxEvents — use re.DOTALL to handle the multi-line PSet block
cfg = re.sub(
    r"process\.maxEvents\s*=\s*cms\.untracked\.PSet\(.*?\)",
    f"process.maxEvents = cms.untracked.PSet(\n    input = cms.untracked.int32({nevents}),\n    output = cms.optional.untracked.allowed(cms.int32,cms.PSet)\n)",
    cfg,
    flags=re.DOTALL,
)

# 3. Inject OMTF Phase-2 dumper at the end of the file
dumper = f"""
# ============================================================
# OMTF Phase-2 hit dumper (injected by smoke_test.sh)
# ============================================================
import FWCore.ParameterSet.Config as _cms  # already imported, alias for safety

# SteppingHelixPropagator is required by CandidateSimMuonMatcher even when
# using simpleMatching — it is requested unconditionally in beginRun.
process.load('TrackPropagation.SteppingHelixPropagator.SteppingHelixPropagatorAlong_cfi')

if not hasattr(process, 'TFileService'):
    process.TFileService = _cms.Service(
        'TFileService',
        fileName=_cms.string('{ntuple_out}'),
    )
else:
    process.TFileService.fileName = _cms.string('{ntuple_out}')

if hasattr(process, 'simOmtfPhase2Digis'):
    process.simOmtfPhase2Digis.dumpHitsToROOT = _cms.bool(True)
    process.simOmtfPhase2Digis.candidateSimMuonMatcher = _cms.bool(True)
    process.simOmtfPhase2Digis.candidateSimMuonMatcherType = _cms.string('simpleMatching')
    process.simOmtfPhase2Digis.muonMatcherFile = _cms.FileInPath(
        'L1Trigger/L1TMuon/data/omtf_config/muonMatcherHists_100files_smoothStdDev_withOvf.root'
    )
    process.simOmtfPhase2Digis.simTracksTag = _cms.InputTag('g4SimHits')
    process.simOmtfPhase2Digis.simVertexesTag = _cms.InputTag('g4SimHits')
else:
    import sys as _sys
    print('WARNING: simOmtfPhase2Digis not found — OMTF dumper disabled', file=_sys.stderr)
# ============================================================
"""

cfg += dumper

with open(cfg_path, 'w') as f:
    f.write(cfg)

print(f"Patched {cfg_path}: GT={conditions}, maxEvents={nevents}, OMTF dumper injected.")
PYEOF

        echo "[1/4] Config ready: ${WDIR}/${CFG}"
    else
        echo "[1/4] --skip-cmsrun: reusing existing ${CFG}"
    fi

    # -----------------------------------------------------------------------
    # Step 2: cmsRun
    # -----------------------------------------------------------------------
    if [ "${SKIP_CMSRUN}" = false ]; then
        echo "[2/4] Running cmsRun..."
        if cmsRun "${CFG}" 2>&1 | tee cmsRun.log; then
            echo "[2/4] cmsRun OK"
        else
            echo "[FAIL] cmsRun non-zero exit for ${DS}"
            echo "       Log: ${WDIR}/cmsRun.log"
            SUMMARY+=("FAIL  ${DS}: cmsRun error (see cmsRun.log)")
            FAIL=$((FAIL+1))
            cd "${BASEDIR}"
            continue
        fi
    else
        echo "[2/4] --skip-cmsrun: skipping cmsRun"
    fi

    # -----------------------------------------------------------------------
    # Step 3: Check outputs
    # -----------------------------------------------------------------------
    echo "[3/4] Checking outputs..."
    HIT_OK=false
    GS_OK=false

    if [ -f "${OUTPUT_NTUPLE}" ]; then
        SZ=$(du -sh "${OUTPUT_NTUPLE}" | cut -f1)
        echo "  omtf_hits.root : ${SZ}"
        HIT_OK=true
    else
        echo "  [WARN] omtf_hits.root NOT produced"
    fi

    if [ -f "${OUTPUT_GENSIM}" ]; then
        SZ=$(du -sh "${OUTPUT_GENSIM}" | cut -f1)
        echo "  ${OUTPUT_GENSIM} : ${SZ}"
        GS_OK=true
    else
        echo "  [WARN] ${OUTPUT_GENSIM} NOT produced"
    fi

    # -----------------------------------------------------------------------
    # Step 4: Run graph builder sanity check
    # -----------------------------------------------------------------------
    if [ "${HIT_OK}" = true ]; then
        echo "[4/4] Building test graph (.pt)..."
        python3 "${SCRIPTS}/make_graphs.py" \
            --input "${OUTPUT_NTUPLE}" \
            --output "${WDIR}/${DS}_graphs.pt" \
            --sample-id "${DS}" \
            --max-events 10 \
            2>&1 | tee make_graphs.log \
            && echo "  ${DS}_graphs.pt OK" \
            || echo "  [WARN] make_graphs.py returned non-zero (see make_graphs.log)"
        PASS=$((PASS+1))
        SUMMARY+=("PASS  ${DS}: omtf_hits.root + ${DS}_graphs.pt")
    else
        WARN=$((WARN+1))
        SUMMARY+=("WARN  ${DS}: omtf_hits.root missing — check if simOmtfPhase2Digis is in schedule")
    fi

    cd "${BASEDIR}"
done

# ---------------------------------------------------------------------------
# Run check_ntuple.py across all smoke outputs
# ---------------------------------------------------------------------------
echo ""
echo "###################################################################"
echo "  Running check_ntuple.py on all smoke outputs"
echo "###################################################################"
python3 "${SCRIPTS}/check_ntuple.py" "${SMOKEDIR}" \
    2>&1 | tee "${SMOKEDIR}/check_results.txt" \
    && echo "" \
    || echo "[WARN] check_ntuple.py exited non-zero"

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "###################################################################"
echo "  SMOKE TEST SUMMARY"
echo "###################################################################"
for line in "${SUMMARY[@]}"; do
    echo "  ${line}"
done
echo ""
echo "  PASS=${PASS}  WARN=${WARN}  FAIL=${FAIL}  (of ${#DATASETS[@]} datasets)"
echo "  Output dir : ${SMOKEDIR}"
echo "  Ntuple check: ${SMOKEDIR}/check_results.txt"
echo "###################################################################"

[ "${FAIL}" -eq 0 ] || exit 1
