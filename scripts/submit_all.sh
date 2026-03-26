#!/bin/bash
###############################################################################
# submit_all.sh — Submit all HTCondor jobs for OMTF HECIN production
###############################################################################
set -euo pipefail

BASEDIR="$HOME/omtf_hecin_dataset_production"
CONDDIR="${BASEDIR}/condor"
EOS_BASE="/eos/cms/store/user/${USER}/omtf_hecin_datasets"

echo "=== OMTF HECIN Full Production Submission ==="
echo ""

# Ensure EOS directories exist
for DS in S1 S3 S4 B1 B2 B3; do
    mkdir -p "${EOS_BASE}/prod/${DS}" 2>/dev/null || true
done

# Ensure run_job.sh is executable
chmod +x "${CONDDIR}/run_job.sh"

# Check for valid proxy
if ! voms-proxy-info --exists 2>/dev/null; then
    echo "ERROR: No valid VOMS proxy found."
    echo "Run:  voms-proxy-init --voms cms --valid 168:00"
    exit 1
fi

echo "Submitting jobs..."
echo ""

for DS in S1 S3 S4 B1 B2 B3; do
    SUBFILE="${CONDDIR}/${DS}.sub"
    if [ ! -f "${SUBFILE}" ]; then
        echo "[SKIP] ${DS}: submit file not found"
        continue
    fi
    echo "--- condor_submit ${DS}.sub ---"
    condor_submit "${SUBFILE}"
    echo ""
done

echo "=== All jobs submitted ==="
echo ""
echo "Monitor with:"
echo "  condor_q ${USER}"
echo ""
echo "Check EOS output:"
echo "  eos ls ${EOS_BASE}/prod/"
echo ""
echo "Remove jobs if needed:"
echo "  condor_rm ${USER}"
