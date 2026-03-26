#!/bin/bash
###############################################################################
# fix_pu_configs.sh — Inject MinBias pileup file list into PU200 configs
#
# Must be run with a valid VOMS proxy:
#   voms-proxy-init --voms cms --valid 168:00
#   bash scripts/fix_pu_configs.sh
###############################################################################
set -euo pipefail

BASEDIR="$HOME/omtf_hecin_dataset_production"
CONFDIR="${BASEDIR}/configs"
PU_DBS_DATASET="/MinBias_TuneCP5_14TeV-pythia8/Phase2Spring24DIGIRECOMiniAOD-PU200_AllTP_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD"

echo "=== Fixing PU200 config file lists ==="
echo ""

if ! voms-proxy-info --exists 2>/dev/null; then
    echo "ERROR: No valid VOMS proxy found."
    echo "Run:  voms-proxy-init --voms cms --valid 168:00"
    exit 1
fi

echo "Querying DAS for MinBias files (may take a moment)..."
PU_FILES=$(dasgoclient --query "file dataset=${PU_DBS_DATASET}" 2>/dev/null \
    | head -200 \
    | awk '{printf "    \"%s\",\n", $1}' )

NFILES=$(echo "${PU_FILES}" | grep -c '\.root' || true)

if [ "${NFILES}" -eq 0 ]; then
    echo "ERROR: dasgoclient returned no files for dataset."
    echo "  ${PU_DBS_DATASET}"
    exit 1
fi

echo "Found ${NFILES} MinBias files."
echo ""

for DS in B1 B2 B3; do
    CFG="${CONFDIR}/${DS}_cfg.py"
    if [ ! -f "${CFG}" ]; then
        echo "[SKIP] ${CFG} not found"
        continue
    fi

    python3 - <<PYEOF
import re
with open('${CFG}') as f:
    cfg = f.read()

file_list = """
${PU_FILES}
""".strip().rstrip(',')

new_block = "process.mix.input.fileNames = cms.untracked.vstring([\n" + file_list + "\n])"

cfg = re.sub(
    r'process\.mix\.input\.fileNames\s*=\s*cms\.untracked\.vstring\(\[.*?\]\)',
    new_block,
    cfg,
    flags=re.DOTALL
)

with open('${CFG}', 'w') as f:
    f.write(cfg)
print('[OK] Patched PU file list in ${CFG}')
PYEOF
done

echo ""
echo "=== PU200 configs updated. ==="
echo "Re-run generate_configs.sh (or run_validation.sh) to proceed."
