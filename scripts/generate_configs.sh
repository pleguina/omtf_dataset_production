#!/bin/bash
###############################################################################
# generate_configs.sh — Generate cmsDriver configs for all datasets
#
# Must be run AFTER setup_cmssw.sh (inside CMSSW environment)
###############################################################################
set -euo pipefail

BASEDIR="$HOME/omtf_hecin_dataset_production"
CMSSW_VERSION="CMSSW_14_2_0_pre2"
CONFDIR="${BASEDIR}/configs"

# Ensure CMSSW environment
if [ -z "${CMSSW_BASE:-}" ]; then
    set +u
    source /cvmfs/cms.cern.ch/cmsset_default.sh
    cd "${BASEDIR}/${CMSSW_VERSION}/src"
    eval "$(scramv1 runtime -sh)"
    set -u
fi

cd "${CONFDIR}"

echo "=== Generating cmsDriver configurations ==="
echo ""

# ---------- shared settings (GEN,SIM and DIGI must use the same geometry/GT) ----------
# Extended2026D110 is the Phase-2 geometry used by the Phase2Spring24GS campaign
# (McM prep_id PPD-Phase2Spring24GS-00002: geometry=Extended2026D110, CMSSW_14_0_6).
# GEN,SIM and DIGI must use identical geometry and GT.
# NOTE: D110 module count changed from 43708 (CMSSW_14_0_6) to 43600 (CMSSW_14_2_0_pre2).
# The T33 alignment payload in the DB has 43708 entries and causes a GeometryMismatch
# unless tracker alignment is disabled (applyAlignment=False) — safe for L1 trigger studies.
GEOMETRY="Extended2026D110"
ERA="Phase2C17I13M9"
CONDITIONS="140X_mcRun4_realistic_v4"
# MinBias pileup: produced with D110 geometry + 140X_mcRun4_realistic_v4 in CMSSW_14_0_6
PILEUP_INPUT="dbs:/MinBias_TuneCP5_14TeV-pythia8/Phase2Spring24GS-140X_mcRun4_realistic_v4-v1/GEN-SIM"
NSTREAMS=3

###########################################################################
# Helper: generate a GEN,SIM config (all datasets — PU is added at the DIGI step)
###########################################################################
gen_config() {
    local DATASET=$1
    local FRAGMENT=$2
    local OUTNAME="${DATASET}_cfg.py"

    echo "--- Generating ${OUTNAME} (GEN,SIM) ---"

    cmsDriver.py "Configuration/GenProduction/python/OMTF_HECIN/${FRAGMENT}" \
        --python_filename "${OUTNAME}" \
        --eventcontent RAWSIM \
        --customise Configuration/DataProcessing/Utils.addMonitoring \
        --datatier GEN-SIM \
        --conditions "${CONDITIONS}" \
        --beamspot HLLHC14TeV \
        --customise_commands 'process.source.numberEventsInLuminosityBlock=cms.untracked.uint32(100)' \
        --step GEN,SIM \
        --geometry "${GEOMETRY}" \
        --era "${ERA}" \
        --fileout "file:${DATASET}.root" \
        --no_exec \
        -n 100 || { echo "ERROR: cmsDriver failed for ${DATASET} GEN,SIM"; return 1; }

    echo "   -> ${OUTNAME} created"
}

###########################################################################
# Helper: generate a DIGI+L1+DIGI2RAW+HLT config (reads GEN-SIM, adds PU)
###########################################################################
b4_config() {
    local OUTNAME="B4_cfg.py"

    echo "--- Generating ${OUTNAME} (DIGI+PU200, no hard-scatter muon) ---"

    cmsDriver.py \
        --python_filename "${OUTNAME}" \
        --eventcontent FEVTDEBUGHLT \
        --pileup AVE_200_BX_25ns \
        --customise "SLHCUpgradeSimulations/Configuration/aging.customise_aging_1000,Configuration/DataProcessing/Utils.addMonitoring" \
        --datatier GEN-SIM-DIGI-RAW \
        --conditions "${CONDITIONS}" \
        --step "DIGI:pdigi_valid,L1TrackTrigger,L1,DIGI2RAW,HLT:@fake2" \
        --geometry "${GEOMETRY}" \
        --nStreams "${NSTREAMS}" \
        --era "${ERA}" \
        --fileout "file:B4_DR.root" \
        --filein "${PILEUP_INPUT}" \
        --pileup_input "${PILEUP_INPUT}" \
        --customise_commands 'process.trackerGeometry.applyAlignment=cms.bool(False)' \
        --no_exec \
        --mc \
        -n -1 || { echo "ERROR: cmsDriver failed for B4"; return 1; }

    echo "   -> ${OUTNAME} created (reads MinBias directly — no separate GEN,SIM step needed)"
}

###########################################################################
# Helper: generate a DIGI+L1+DIGI2RAW+HLT config (reads GEN-SIM, adds PU)
###########################################################################
digi_config() {
    local DATASET=$1
    local OUTNAME="${DATASET}_DR_cfg.py"

    echo "--- Generating ${OUTNAME} (DIGI+L1+DIGI2RAW+HLT, PU200) ---"

    cmsDriver.py \
        --python_filename "${OUTNAME}" \
        --eventcontent FEVTDEBUGHLT \
        --pileup AVE_200_BX_25ns \
        --customise "SLHCUpgradeSimulations/Configuration/aging.customise_aging_1000,Configuration/DataProcessing/Utils.addMonitoring" \
        --datatier GEN-SIM-DIGI-RAW \
        --conditions "${CONDITIONS}" \
        --step "DIGI:pdigi_valid,L1TrackTrigger,L1,DIGI2RAW,HLT:@fake2" \
        --geometry "${GEOMETRY}" \
        --nStreams "${NSTREAMS}" \
        --era "${ERA}" \
        --fileout "file:${DATASET}_DR.root" \
        --filein "file:${DATASET}.root" \
        --pileup_input "${PILEUP_INPUT}" \
        --customise_commands 'process.trackerGeometry.applyAlignment=cms.bool(False)' \
        --no_exec \
        --mc \
        -n -1 || { echo "ERROR: cmsDriver failed for ${DATASET} DIGI"; return 1; }

    echo "   -> ${OUTNAME} created"
}

###########################################################################
# Generate all configs
###########################################################################

# --- Step 1: GEN,SIM configs for all datasets ---
echo "=== Step 1: GEN,SIM configs ==="
# Prompt samples
gen_config "S1" "S1_singleMuon_1overPt.py"
gen_config "S3" "S3_diMuon_sameSector.py"
gen_config "S4" "S4_triMuon_sameSector.py"
gen_config "B1" "B1_singleMuon_PU200.py"
gen_config "B2" "B2_displacedMuon_PU200.py"
gen_config "B3" "B3_diMuon_PU200.py"
gen_config "D1" "D1_displacedMuon_LLP.py"
# Displaced samples: use FlatRandomLLPGunProducer2 (ctau-based displacement,
# no vertex smearing needed). The standard gen_config helper is used.
# NOTE: muon pT from the LLP gun is NOT flat in 1/pT. Apply training weight
# w = 1/pT^2 per event in the PyTorch DataLoader using the 'gen_muon_pt' field.
#   S2 (single mu-): PartID=[-13], ctau 0.1-1000 mm -> d0 in [0, ~50 cm]
#   S5 (mu- + mu+):  PartID=[-13,13], ctau 0.1-500 mm -> d0 in [0, ~30 cm]
gen_config "S2" "S2_displacedMuon_flatVertex.py"
gen_config "S5" "S5_diMuon_displaced.py"

# --- Step 2: DIGI+L1+DIGI2RAW+HLT configs for all datasets (adds PU200) ---
echo ""
echo "=== Step 2: DIGI+L1+DIGI2RAW+HLT configs (PU200) ==="
digi_config "S1"
digi_config "S2"
digi_config "S3"
digi_config "S4"
digi_config "S5"
digi_config "B1"
digi_config "B2"
digi_config "B3"
digi_config "D1"

# --- Step 3: B4 (noise-only DIGI — no GEN,SIM step, reads MinBias directly) ---
echo ""
echo "=== Step 3: B4 noise-only config (DIGI-only, reads MinBias GEN-SIM from DBS) ==="
b4_config

echo ""
echo "=== All configurations generated in ${CONFDIR} ==="
ls -la "${CONFDIR}"/*.py

echo ""
echo "Configs per dataset:"
echo "  <DS>_cfg.py    — GEN,SIM  (outputs <DS>.root)"
echo "  <DS>_DR_cfg.py — DIGI+L1+DIGI2RAW+HLT with PU200 (inputs <DS>.root, outputs <DS>_DR.root)"
echo "  B4_cfg.py      — DIGI-only, reads MinBias from DBS directly (no GEN,SIM step)"
