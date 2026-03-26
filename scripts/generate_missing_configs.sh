#!/bin/bash
###############################################################################
# generate_missing_configs.sh — Generate cmsDriver configs for S2, S5, B4
#                               and regenerate S3, S4, B3 from updated fragments
#
# Must be run inside the CMSSW environment:
#   cd ~/omtf_hecin_dataset_production/CMSSW_14_2_0_pre2/src
#   cmsenv   (or: eval $(scramv1 runtime -sh))
#   bash ~/omtf_hecin_dataset_production/scripts/generate_missing_configs.sh
#
# What it does:
#   1. Copies all updated fragments to the CMSSW GEN package path
#   2. Generates S2_cfg.py, S5_cfg.py (GEN+SIM+DIGI+L1, no PU)
#   3. Generates B4_cfg.py (DIGI-only, reads MinBias from DBS)
#   4. Optionally regenerates S3, S4, B3 (pass --regen-multi flag)
#   5. Appends the OMTF dumper customizer to each generated config
###############################################################################
set -euo pipefail

BASEDIR="$(realpath "$(dirname "$0")/..")"
CMSSW_VERSION="CMSSW_14_2_0_pre2"
FRAGDIR="${BASEDIR}/fragments"
CONFDIR="${BASEDIR}/configs"
CMSSW_FRAGDIR="${BASEDIR}/${CMSSW_VERSION}/src/Configuration/GenProduction/python/OMTF_HECIN"

REGEN_MULTI=false
for arg in "$@"; do
    [[ "${arg}" == "--regen-multi" ]] && REGEN_MULTI=true
done

# ── Verify CMSSW environment ─────────────────────────────────────────────────
if [ -z "${CMSSW_BASE:-}" ]; then
    echo "ERROR: CMSSW environment not set."
    echo "  Run: cd ${BASEDIR}/${CMSSW_VERSION}/src && cmsenv"
    exit 1
fi
echo "CMSSW_BASE = ${CMSSW_BASE}"

cd "${CONFDIR}"

# ── Shared settings (must match existing S1/B1 configs) ──────────────────────
GEOMETRY="ExtendedRun4D110"
ERA="Phase2C17I13M9"
CONDITIONS="auto:phase2_realistic_T33"
BEAMSPOT="HLLHC14TeV"
# Premixed PU200 dataset (used by B1/B2/B3; same should be used for B3 regen)
PREMIXED_PU="dbs:/MinBias_TuneCP5_14TeV-pythia8/Phase2Spring24DIGIRECOMiniAOD-PU200_AllTP_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD"
# GEN-SIM MinBias for B4 (overlay minbias as "signal")
GENSIM_MINBIAS="dbs:/MinBias_TuneCP5_14TeV-pythia8/Phase2Spring24GS-140X_mcRun4_realistic_v4-v1/GEN-SIM"

# ── Step 1: deploy updated fragments to CMSSW package path ───────────────────
echo ""
echo "=== Step 1: Copying fragments to CMSSW package ==="
mkdir -p "${CMSSW_FRAGDIR}"
for frag in S2_displacedMuon_flatVertex.py S5_diMuon_displaced.py \
            S3_diMuon_sameSector.py S4_triMuon_sameSector.py \
            B2_displacedMuon_PU200.py B3_diMuon_PU200.py \
            B1_singleMuon_PU200.py; do
    src="${FRAGDIR}/${frag}"
    dst="${CMSSW_FRAGDIR}/${frag}"
    if [ -f "${src}" ]; then
        cp -v "${src}" "${dst}"
    else
        echo "WARNING: fragment not found: ${src}"
    fi
done
echo "   Fragments deployed."

# ── Helper: append OMTF dumper customizer to a generated config ──────────────
append_dumper() {
    local cfg_file="$1"
    cat >> "${cfg_file}" <<'DUMPER'

# ========================================================================
# OMTF Phase-2 dumper customisation — produce OMTFHitsTree
# ========================================================================
from customize_omtf_dumper import customise_omtf_dumper
process = customise_omtf_dumper(process)
DUMPER
    echo "   -> OMTF dumper appended to ${cfg_file}"
}

# ── Step 2: S2 — single displaced muon, flat d0, no PU ───────────────────────
echo ""
echo "=== Step 2: Generating S2_cfg.py (displaced muon, flatOneOverPt, flatD0) ==="
cmsDriver.py "Configuration/GenProduction/python/OMTF_HECIN/S2_displacedMuon_flatVertex.py" \
    --python_filename S2_cfg.py \
    --eventcontent FEVTSIM \
    --datatier GEN-SIM-DIGI-RAW \
    --conditions "${CONDITIONS}" \
    --geometry "${GEOMETRY}" \
    --era "${ERA}" \
    --beamspot "${BEAMSPOT}" \
    --step GEN,SIM,DIGI:pdigi_valid,L1 \
    --nThreads 4 \
    --fileout "file:S2.root" \
    --no_exec \
    -n 100
append_dumper S2_cfg.py

# ── Step 3: S5 — two displaced muons, independent flat d0, no PU ─────────────
echo ""
echo "=== Step 3: Generating S5_cfg.py (two displaced muons, flatD0 [0,30cm]) ==="
cmsDriver.py "Configuration/GenProduction/python/OMTF_HECIN/S5_diMuon_displaced.py" \
    --python_filename S5_cfg.py \
    --eventcontent FEVTSIM \
    --datatier GEN-SIM-DIGI-RAW \
    --conditions "${CONDITIONS}" \
    --geometry "${GEOMETRY}" \
    --era "${ERA}" \
    --beamspot "${BEAMSPOT}" \
    --step GEN,SIM,DIGI:pdigi_valid,L1 \
    --nThreads 4 \
    --fileout "file:S5.root" \
    --no_exec \
    -n 100
append_dumper S5_cfg.py

# ── Step 4: B4 — noise-only PU200 (DIGI-only, no hard-scatter muon) ──────────
echo ""
echo "=== Step 4: Generating B4_cfg.py (noise-only PU200, DIGI step only) ==="
cmsDriver.py \
    --python_filename B4_cfg.py \
    --eventcontent FEVTSIM \
    --pileup AVE_200_BX_25ns \
    --datatier GEN-SIM-DIGI-RAW \
    --conditions "${CONDITIONS}" \
    --step DIGI:pdigi_valid,L1 \
    --geometry "${GEOMETRY}" \
    --era "${ERA}" \
    --nThreads 4 \
    --fileout "file:B4.root" \
    --filein "${GENSIM_MINBIAS}" \
    --pileup_input "${GENSIM_MINBIAS}" \
    --no_exec \
    --mc \
    -n 100
append_dumper B4_cfg.py

# ── Optional Step 5: Regenerate S3, S4, B3 from updated fragments ────────────
if [ "${REGEN_MULTI}" = "true" ]; then
    echo ""
    echo "=== Step 5: Regenerating S3, S4, B3 from updated fragments ==="
    echo "    (phi constrained to +/-pi/3, FlatRandomPtGunProducer2)"

    cmsDriver.py "Configuration/GenProduction/python/OMTF_HECIN/S3_diMuon_sameSector.py" \
        --python_filename S3_cfg.py \
        --eventcontent FEVTSIM \
        --datatier GEN-SIM-DIGI-RAW \
        --conditions "${CONDITIONS}" \
        --geometry "${GEOMETRY}" \
        --era "${ERA}" \
        --step GEN,SIM,DIGI:pdigi_valid,L1 \
        --nThreads 4 \
        --fileout "file:S3.root" \
        --no_exec \
        -n 100
    append_dumper S3_cfg.py

    cmsDriver.py "Configuration/GenProduction/python/OMTF_HECIN/S4_triMuon_sameSector.py" \
        --python_filename S4_cfg.py \
        --eventcontent FEVTSIM \
        --datatier GEN-SIM-DIGI-RAW \
        --conditions "${CONDITIONS}" \
        --geometry "${GEOMETRY}" \
        --era "${ERA}" \
        --step GEN,SIM,DIGI:pdigi_valid,L1 \
        --nThreads 4 \
        --fileout "file:S4.root" \
        --no_exec \
        -n 100
    append_dumper S4_cfg.py

    cmsDriver.py "Configuration/GenProduction/python/OMTF_HECIN/B3_diMuon_PU200.py" \
        --python_filename B3_cfg.py \
        --eventcontent FEVTSIM \
        --datatier GEN-SIM-DIGI-RAW \
        --conditions "${CONDITIONS}" \
        --geometry "${GEOMETRY}" \
        --era "${ERA}" \
        --step GEN,SIM,DIGI:pdigi_valid,L1 \
        --nThreads 4 \
        --pileup AVE_200_BX_25ns \
        --pileup_input "${PREMIXED_PU}" \
        --fileout "file:B3.root" \
        --no_exec \
        -n 100
    append_dumper B3_cfg.py
else
    echo ""
    echo "--- Skipping S3/S4/B3 regen (pass --regen-multi to include) ---"
    echo "    Existing configs work for testset pipeline validation."
fi

echo ""
echo "=== Done. Generated configs in: ${CONFDIR}/ ==="
ls -la S2_cfg.py S5_cfg.py B4_cfg.py 2>/dev/null
echo ""
echo "Next: uncomment S2, S5, B4 rows in condor/testset.sub and re-submit."
