#!/bin/bash
###############################################################################
# estimate_storage.sh — Estimate full production storage from validation runs
###############################################################################
set -euo pipefail

BASEDIR="$HOME/omtf_hecin_dataset_production"
TESTDIR="${BASEDIR}/test"

echo "==================================================================="
echo "  OMTF HECIN Dataset Production — Storage Estimation"
echo "==================================================================="
echo ""

# Dataset name, target events, events-per-job
declare -A TARGET_EVENTS
TARGET_EVENTS[S1]=1000000
TARGET_EVENTS[S3]=250000
TARGET_EVENTS[S4]=100000
TARGET_EVENTS[B1]=500000
TARGET_EVENTS[B2]=200000
TARGET_EVENTS[B3]=100000

DATASETS=("S1" "S3" "S4" "B1" "B2" "B3")
TOTAL_GB=0

printf "%-8s %12s %12s %14s %14s\n" \
       "Dataset" "Target_Evts" "Bytes/Event" "Est_Size(GB)" "Test_File(MB)"
printf "%-8s %12s %12s %14s %14s\n" \
       "-------" "-----------" "-----------" "-------------" "--------------"

for DS in "${DATASETS[@]}"; do
    WORKDIR="${TESTDIR}/${DS}"
    HITS_FILE="${WORKDIR}/omtf_hits.root"
    FEVT_FILE="${WORKDIR}/${DS}.root"

    # Prefer the TFileService output (omtf_hits.root) for size estimation
    # since that's what we actually store in production
    if [ -f "${HITS_FILE}" ]; then
        CHECK_FILE="${HITS_FILE}"
    elif [ -f "${FEVT_FILE}" ]; then
        CHECK_FILE="${FEVT_FILE}"
    else
        printf "%-8s %12d %12s %14s %14s\n" "${DS}" "${TARGET_EVENTS[$DS]}" "N/A" "N/A" "N/A"
        continue
    fi

    FILE_BYTES=$(stat --printf="%s" "${CHECK_FILE}")
    FILE_MB=$(echo "scale=2; ${FILE_BYTES} / 1048576" | bc)

    # Count entries in the tree for accurate per-event estimate
    NENTRIES=$(python3 -c "
import ROOT
f = ROOT.TFile.Open('${CHECK_FILE}')
if f and not f.IsZombie():
    for tname in ['simOmtfPhase2Digis/OMTFHitsTree', 'OMTFHitsTree']:
        t = f.Get(tname)
        if t:
            print(int(t.GetEntries()))
            break
    else:
        # If no tree, estimate from Events tree or assume test events=1000
        t = f.Get('Events')
        if t:
            print(int(t.GetEntries()))
        else:
            print(1000)
else:
    print(1000)
" 2>/dev/null || echo "1000")

    if [ "${NENTRIES}" -le 0 ]; then
        NENTRIES=1000
    fi

    BYTES_PER_EVENT=$(echo "scale=0; ${FILE_BYTES} / ${NENTRIES}" | bc)
    TARGET=${TARGET_EVENTS[$DS]}
    EST_BYTES=$(echo "${BYTES_PER_EVENT} * ${TARGET}" | bc)
    EST_GB=$(echo "scale=2; ${EST_BYTES} / 1073741824" | bc)

    printf "%-8s %12d %12d %14s %14s\n" \
           "${DS}" "${TARGET}" "${BYTES_PER_EVENT}" "${EST_GB}" "${FILE_MB}"

    TOTAL_GB=$(echo "scale=2; ${TOTAL_GB} + ${EST_GB}" | bc)
done

echo ""
echo "==================================================================="
printf "  TOTAL ESTIMATED STORAGE:  %s GB\n" "${TOTAL_GB}"
echo "==================================================================="
echo ""
echo "Expected range: 200–350 GB total."
echo "If significantly different, investigate event content or PU settings."
