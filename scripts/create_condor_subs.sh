#!/bin/bash
###############################################################################
# create_condor_subs.sh — Generate all HTCondor .sub files
###############################################################################
set -euo pipefail

BASEDIR="$HOME/omtf_hecin_dataset_production"
CONDDIR="${BASEDIR}/condor"
LOGDIR="${BASEDIR}/logs"

mkdir -p "${LOGDIR}"

# Dataset definitions:  NAME  TARGET_EVENTS  EVENTS_PER_JOB  FLAVOUR
# PU=0 samples:  20000 events/job
# PU200 samples:  5000 events/job
declare -a CONFIGS=(
    "S1  1000000 20000 tomorrow"
    "S3   250000 20000 tomorrow"
    "S4   100000 20000 tomorrow"
    "B1   500000  5000 testmatch"
    "B2   200000  5000 testmatch"
    "B3   100000  5000 testmatch"
)

echo "=== Creating HTCondor submission files ==="
echo ""

for entry in "${CONFIGS[@]}"; do
    read -r DS TARGET EPJ FLAVOUR <<< "${entry}"

    NJOBS=$(( (TARGET + EPJ - 1) / EPJ ))

    SUBFILE="${CONDDIR}/${DS}.sub"

    cat > "${SUBFILE}" <<EOF
###############################################################################
# ${DS}.sub — HTCondor submit for OMTF HECIN dataset ${DS}
# Target: ${TARGET} events, ${EPJ} events/job, ${NJOBS} jobs
###############################################################################
universe        = vanilla
executable      = ${CONDDIR}/run_job.sh
arguments       = ${DS} \$(ProcId) ${EPJ}

output          = ${LOGDIR}/${DS}_\$(ProcId).out
error           = ${LOGDIR}/${DS}_\$(ProcId).err
log             = ${LOGDIR}/${DS}.log

transfer_input_files = ${BASEDIR}/configs/${DS}_cfg.py
should_transfer_files = NO

request_cpus    = 1
request_memory  = 4000
request_disk    = 10000000

+JobFlavour     = "${FLAVOUR}"

# Use CMS submit infrastructure
+AccountingGroup = "group_u_CMS.u_zh.users"
use_x509userproxy = true

queue ${NJOBS}
EOF

    echo "  ${DS}.sub  -> ${NJOBS} jobs x ${EPJ} events = $((NJOBS * EPJ)) events (target: ${TARGET})"
done

echo ""
echo "=== All .sub files created in ${CONDDIR}/ ==="
