#!/usr/bin/env bash
###############################################################################
# run_das_smoke_test.sh — 1-file, N-event smoke test for all first-validation
# DAS datasets using the das_reemul_cfg.py L1 re-emulation config.
#
# Usage: ./run_das_smoke_test.sh [NEVENTS]
#   NEVENTS  number of events per dataset (default: 20)
#
# Outputs:
#   smoke_results/das_smoke_<timestamp>/DAS_SMOKE_REPORT.md
#   smoke_results/das_smoke_<timestamp>/<label>_omtf_hits.root
#   smoke_results/das_smoke_<timestamp>/<label>_omtf_nano.root
#   smoke_results/das_smoke_<timestamp>/<label>_cmsrun.log
###############################################################################
set -euo pipefail

NEVENTS="${1:-20}"

BASEDIR="/afs/cern.ch/user/p/pleguina/omtf_dataset_production"
CMSSW_DIR="/afs/cern.ch/user/p/pleguina/CMSSW_14_2_0_pre2"
CONFDIR="${BASEDIR}/configs"
OUTDIR="${BASEDIR}/smoke_results/das_smoke_$(date +%Y%m%d_%H%M%S)"
REPORT="${OUTDIR}/DAS_SMOKE_REPORT.md"

export X509_USER_PROXY="/afs/cern.ch/user/p/pleguina/private/x509_proxy"

mkdir -p "${OUTDIR}"

# label | global_tag | smoke_input_file
# Disk-accessible files for each validation class.
# All datasets use Spring24/140X disk alternatives (Fall22 versions are tape-only).
SPECS=$(cat <<'EOF'
minbias|140X_mcRun4_realistic_v4|/store/mc/Phase2Spring24DIGIRECOMiniAOD/MinBias_TuneCP5_14TeV-pythia8/GEN-SIM-DIGI-RAW-MINIAOD/PU140_Trk1GeV_140X_mcRun4_realistic_v4-v1/120000/a7bb849a-2434-4c23-836e-ebaaafdc5f2d.root
displaced_lowpt|140X_mcRun4_realistic_v4|/store/mc/Phase2Spring24DIGIRECOMiniAOD/DisplacedMuons_Pt-2To10_Dxy-0To3000-gun/GEN-SIM-DIGI-RAW-MINIAOD/PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/130000/3acd9ce2-a987-4192-9e96-d9050add5eb2.root
displaced_midpt|140X_mcRun4_realistic_v4|/store/mc/Phase2Spring24DIGIRECOMiniAOD/DisplacedMuons_Pt-10To30_Dxy-0To3000-gun/GEN-SIM-DIGI-RAW-MINIAOD/PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/2810000/4c5da4c2-0307-4e29-b55c-3eb069c98342.root
dy_prompt|140X_mcRun4_realistic_v4|/store/mc/Phase2Spring24DIGIRECOMiniAOD/DYToLL_M-10To50_TuneCP5_14TeV-pythia8/GEN-SIM-DIGI-RAW-MINIAOD/PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/2560000/5a7d75f3-dcc2-483c-a840-0d66fbd1acbe.root
llp_addon|140X_mcRun4_realistic_v4|/store/mc/Phase2Spring24DIGIRECOMiniAOD/HTo2LongLivedTo4mu_MH-125_MFF-12_CTau-900mm_TuneCP5_14TeV-pythia8/GEN-SIM-DIGI-RAW-MINIAOD/PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/2530000/92ddf443-bd16-445d-a951-0440c576434f.root
single_muon_flatpt|140X_mcRun4_realistic_v4|/store/mc/Phase2Spring24DIGIRECOMiniAOD/SingleMu_FlatPt-2to100/GEN-SIM-DIGI-RAW-MINIAOD/PU200_Trk1GeV_140X_mcRun4_realistic_v4-v2/120000/0089c8ae-7b77-411a-9afd-3dfda70748d0.root
EOF
)

# --- CMSSW environment (set up once) ---
set +u
source /cvmfs/cms.cern.ch/cmsset_default.sh
set -u
export SCRAM_ARCH="el9_amd64_gcc12"
cd "${CMSSW_DIR}/src"
set +u; eval "$(scramv1 runtime -sh)"; set -u

# --- Report header ---
cat > "${REPORT}" <<'HEADER'
# DAS Smoke Test Report

| Label | GT | Events | cmsRun | OMTFAllInputTree | OMTFHitsTree | MuonStubTps | MuonStubKmtf | GenMuon |
|---|---|---|---|---|---|---|---|---|
HEADER

# --- Process each dataset ---
while IFS='|' read -r label gt input_file; do
    [[ -z "${label}" || "${label}" == \#* ]] && continue

    echo "=== ${label} (GT=${gt}, ${NEVENTS} events) ==="
    date

    SCRATCH="/tmp/${USER}_das_smoke_${label}_$$"
    mkdir -p "${SCRATCH}"
    OMTF_OUT="omtf_hits_${label}.root"
    NANO_OUT="omtf_nano_${label}.root"

    cp "${CONFDIR}/das_reemul_cfg.py"        "${SCRATCH}/job_cfg.py"
    cp "${CONFDIR}/customize_omtf_dumper.py" "${SCRATCH}/"

    cd "${SCRATCH}"

    export DAS_INPUT_FILE="${input_file}"
    export DAS_GLOBAL_TAG="${gt}"
    export DAS_OMTF_OUTPUT="${OMTF_OUT}"
    export DAS_NANO_OUTPUT="${NANO_OUT}"
    export DAS_MAX_EVENTS="${NEVENTS}"

    STATUS="FAIL"
    if cmsRun job_cfg.py > "cmsrun_${label}.log" 2>&1; then
        STATUS="OK"
    else
        echo "  [WARN] cmsRun FAILED — see ${OUTDIR}/${label}_cmsrun.log"
    fi

    # Check ROOT TFileService branches
    check_tfs_branch() {
        local treepath="$1"
        if [[ "${STATUS}" == "OK" ]] && python3 - <<PYEOF 2>/dev/null
import ROOT, sys
f = ROOT.TFile.Open("${OMTF_OUT}")
if not f or f.IsZombie(): sys.exit(1)
parts = "${treepath}".split("/")
obj = f
for p in parts:
    obj = obj.Get(p)
    if not obj: sys.exit(1)
sys.exit(0)
PYEOF
        then echo "OK"; else echo "MISS"; fi
    }

    # Check NanoAOD branch prefix
    check_nano_branch() {
        local prefix="$1"
        if [[ "${STATUS}" == "OK" ]] && python3 - <<PYEOF 2>/dev/null
import ROOT, sys
f = ROOT.TFile.Open("${NANO_OUT}")
if not f or f.IsZombie(): sys.exit(1)
t = f.Get("Events")
if not t: sys.exit(1)
found = any(b.GetName().startswith("${prefix}") for b in t.GetListOfBranches())
sys.exit(0 if found else 1)
PYEOF
        then echo "OK"; else echo "MISS"; fi
    }

    ALLINTREE=$(check_tfs_branch "simOmtfPhase2Digis/OMTFAllInputTree")
    HITSTREE=$(check_tfs_branch  "simOmtfPhase2Digis/OMTFHitsTree")
    MUONSTUBS=$(check_nano_branch "MuonStubTps")
    MUONSTUBK=$(check_nano_branch "MuonStubKmtf")
    GENMUON=$(check_nano_branch   "GenMuon")

    echo "  OMTFAllInputTree=${ALLINTREE}  OMTFHitsTree=${HITSTREE}  MuonStubTps=${MUONSTUBS}  MuonStubKmtf=${MUONSTUBK}  GenMuon=${GENMUON}"

    # Save artefacts
    cp "${OMTF_OUT}"         "${OUTDIR}/${label}_omtf_hits.root"  2>/dev/null || true
    cp "${NANO_OUT}"         "${OUTDIR}/${label}_omtf_nano.root"  2>/dev/null || true
    cp "cmsrun_${label}.log" "${OUTDIR}/${label}_cmsrun.log"       2>/dev/null || true

    echo "| ${label} | ${gt} | ${NEVENTS} | ${STATUS} | ${ALLINTREE} | ${HITSTREE} | ${MUONSTUBS} | ${MUONSTUBK} | ${GENMUON} |" \
        >> "${REPORT}"

    cd "${BASEDIR}"
    rm -rf "${SCRATCH}"

done <<< "${SPECS}"

echo ""
echo "=== DAS smoke test complete ==="
echo "Report : ${REPORT}"
cat "${REPORT}"
