#!/bin/bash
###############################################################################
# run_validation.sh — Run 1000-event validation for each dataset
#
# Run AFTER generate_configs.sh.  Produces test output in EOS test/ area.
###############################################################################
set -euo pipefail

BASEDIR="$HOME/omtf_hecin_dataset_production"
CMSSW_VERSION="CMSSW_14_2_0_pre2"
CONFDIR="${BASEDIR}/configs"
TESTDIR="${BASEDIR}/test"
EOS_TEST="/eos/cms/store/user/${USER}/omtf_hecin_datasets/test"
NEVENTS=1000

# Ensure CMSSW environment
if [ -z "${CMSSW_BASE:-}" ]; then
    set +u
    source /cvmfs/cms.cern.ch/cmsset_default.sh
    cd "${BASEDIR}/${CMSSW_VERSION}/src"
    eval "$(scramv1 runtime -sh)"
    set -u
fi

# Create EOS test directory (may not be mounted; non-fatal)
mkdir -p "${EOS_TEST}" 2>/dev/null || true
mkdir -p "${TESTDIR}"

DATASETS=("S1" "S2" "S3" "S4" "S5" "B1" "B2" "B3" "D1")
PASS=0
FAIL=0

echo "=== OMTF HECIN Validation — ${NEVENTS} events per dataset ==="
echo ""

for DS in "${DATASETS[@]}"; do
    CFG="${CONFDIR}/${DS}_cfg.py"

    if [ ! -f "${CFG}" ]; then
        echo "[SKIP] ${DS}: config not found at ${CFG}"
        FAIL=$((FAIL + 1))
        continue
    fi

    echo "---------------------------------------------------------------"
    echo "[RUN]  ${DS}: cmsRun ${DS}_cfg.py  maxEvents=${NEVENTS}"
    echo "---------------------------------------------------------------"

    WORKDIR="${TESTDIR}/${DS}"
    mkdir -p "${WORKDIR}"
    cd "${WORKDIR}"

    # Copy config only (no OMTF dumper at GEN,SIM step)
    cp "${CFG}" .

    # Patch maxEvents in the local copy (cmsDriver hard-codes -n 100)
    python3 -c "
import re, sys
with open('${DS}_cfg.py') as f:
    cfg = f.read()
cfg = re.sub(
    r'(input\s*=\s*cms\.untracked\.int32\()\d+(\))',
    r'\g<1>${NEVENTS}\2',
    cfg, count=1
)
with open('${DS}_cfg.py', 'w') as f:
    f.write(cfg)
"

    # Run cmsRun
    if cmsRun "${DS}_cfg.py" 2>&1 | tee "cmsRun_${DS}.log"; then
        echo "[OK]   ${DS}: cmsRun completed"
    else
        echo "[FAIL] ${DS}: cmsRun returned non-zero exit code"
        FAIL=$((FAIL + 1))
        continue
    fi

    # Check GEN-SIM RAWSIM output file
    ROOT_FILE="${DS}.root"
    if [ -f "${ROOT_FILE}" ]; then
        FSIZE=$(stat --printf="%s" "${ROOT_FILE}")
        echo "[OK]   ${DS}: GEN-SIM ROOT file exists (${FSIZE} bytes)"
    else
        echo "[FAIL] ${DS}: output ROOT file not found"
        FAIL=$((FAIL + 1))
        continue
    fi

    # Copy GEN-SIM output to EOS
    xrdcp --force "${ROOT_FILE}" "root://eoscms.cern.ch/${EOS_TEST}/${DS}.root" 2>/dev/null \
        || cp "${ROOT_FILE}" "${EOS_TEST}/${DS}.root" 2>/dev/null \
        || echo "[WARN] ${DS}: Could not copy to EOS"

    PASS=$((PASS + 1))
    echo ""
done

echo "==============================================================="
echo "Validation summary: ${PASS} passed, ${FAIL} failed out of ${#DATASETS[@]}"
echo "EOS test area: ${EOS_TEST}"
echo "==============================================================="
