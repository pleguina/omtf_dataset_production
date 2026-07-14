#!/bin/bash
###############################################################################
# cleanup_condor_logs.sh — prune per-job HTCondor stdout/stderr after a
# CONFIRMED successful run, so log accumulation never threatens the AFS quota.
#
# Design notes:
#   - All condor .sub files in this repo write output/error/log to AFS
#     (condor/logs/, right next to the .sub files) — that's the whole point
#     of this script: it is the ONLY thing keeping that directory from
#     growing without bound while a production campaign with thousands of
#     jobs runs against a 10GB AFS quota.
#   - Meant to be invoked repeatedly by cleanup_watcher_loop.sh (a persistent
#     loop, itself submitted as an HTCondor job — see condor/cleanup_watcher.sub
#     and the comments there for why: personal crontab is disabled for this
#     account on lxplus, and a plain nohup'd shell does not reliably survive
#     logging back in on a different lxplus node).
#   - Only removes logs for jobs that:
#       1. appear in condor_history with ExitCode == 0, AND
#       2. have a corresponding non-trivial output ROOT file already present
#          on EOS (defensive: condor ExitCode 0 alone does not prove the
#          ROOT file is valid/complete).
#   - Failed/held/removed jobs are NEVER touched — their logs are kept for
#     debugging until you deal with them manually.
#   - A small "seen" state file (on AFS, negligible size — a few bytes per
#     job) records every ClusterId.ProcId already evaluated so repeated
#     passes don't redo work. Loaded into memory ONCE per pass.
#   - Field-splitting uses bash's builtin `read`, not per-line awk/grep
#     subprocess forks, to keep a pass over a few thousand history lines fast.
#   - Idempotent / safe to re-run: already-deleted files are simply skipped.
###############################################################################
set -uo pipefail

BASEDIR="/afs/cern.ch/user/p/pleguina/omtf_dataset_production"
AFS_LOGS="${BASEDIR}/condor/logs"
EOS_PROD="/eos/user/p/pleguina/omtf_hecin_datasets/prod"
SELF_LOG="${AFS_LOGS}/_cleanup_watcher.log"
STATE_FILE="${AFS_LOGS}/_cleanup_watcher_seen.txt"
MIN_ROOT_BYTES=2000   # a genuinely empty/broken omtf_hits_*.root is far smaller than this
# How far back into condor_history to look each pass. Bounded on purpose —
# see design note above. Override with CLEANUP_HISTORY_LIMIT if ever needed.
HIST_LIMIT="${CLEANUP_HISTORY_LIMIT:-1500}"

mkdir -p "${AFS_LOGS}"
touch "${STATE_FILE}"

ts() { date '+%Y-%m-%d %H:%M:%S'; }

{
echo "[$(ts)] cleanup pass starting (host=$(hostname -s), history-limit=${HIST_LIMIT})"

# Load the state file ONCE into memory: SEEN["cluster.proc"]=1
declare -A SEEN
while IFS=' ' read -r key _status _dataset; do
    [ -n "${key}" ] && SEEN["${key}"]=1
done < "${STATE_FILE}"

mapfile -t HIST_LINES < <(timeout -k 5 60 condor_history "${USER}" -af ClusterId ProcId ExitCode Args -limit "${HIST_LIMIT}" 2>/dev/null)

N_TOTAL=0
N_NEW=0
N_CLEANED=0
N_SKIPPED_NOTOK=0
N_SKIPPED_NOFILE=0
NEW_STATE_LINES=()

for line in "${HIST_LINES[@]}"; do
    [ -z "${line}" ] && continue
    read -r CLUSTER PROC EXITCODE DATASET _rest <<< "${line}"
    [ -z "${DATASET}" ] && continue
    N_TOTAL=$((N_TOTAL + 1))

    KEY="${CLUSTER}.${PROC}"
    if [ -n "${SEEN[${KEY}]+x}" ]; then
        continue   # already evaluated in a previous pass — skip all EOS I/O
    fi
    N_NEW=$((N_NEW + 1))

    if [ "${EXITCODE}" != "0" ]; then
        N_SKIPPED_NOTOK=$((N_SKIPPED_NOTOK + 1))
        NEW_STATE_LINES+=("${KEY} FAILED ${DATASET}")
        SEEN["${KEY}"]=1
        continue
    fi

    HITS_FILE="${EOS_PROD}/${DATASET}/omtf_hits_${DATASET}_${PROC}.root"
    if [ ! -f "${HITS_FILE}" ]; then
        N_SKIPPED_NOFILE=$((N_SKIPPED_NOFILE + 1))
        # Do NOT mark as seen: output may still be uploading / propagating on
        # EOS; retry this ClusterId.ProcId again on the next pass.
        continue
    fi
    SIZE=$(stat -c%s "${HITS_FILE}" 2>/dev/null || echo 0)
    if [ "${SIZE}" -lt "${MIN_ROOT_BYTES}" ]; then
        N_SKIPPED_NOFILE=$((N_SKIPPED_NOFILE + 1))
        continue
    fi

    # Match any historical naming convention used across .sub files:
    #   $(DS)_$(ProcId).out / full_$(Dataset)_$(ProcId).out / gmt_$(Dataset)_$(Process).out
    REMOVED_ANY=0
    for f in "${AFS_LOGS}"/*"${DATASET}_${PROC}".out "${AFS_LOGS}"/*"${DATASET}_${PROC}".err; do
        [ -e "${f}" ] || continue
        rm -f -- "${f}" && REMOVED_ANY=1
    done
    if [ "${REMOVED_ANY}" -eq 1 ]; then
        N_CLEANED=$((N_CLEANED + 1))
    fi
    NEW_STATE_LINES+=("${KEY} OK ${DATASET}")
    SEEN["${KEY}"]=1
done

# One single append to the (EOS-hosted) state file instead of one per line.
if [ "${#NEW_STATE_LINES[@]}" -gt 0 ]; then
    printf '%s\n' "${NEW_STATE_LINES[@]}" >> "${STATE_FILE}"
fi

echo "[$(ts)] pass done: scanned=${N_TOTAL} new=${N_NEW} cleaned=${N_CLEANED} kept(failed)=${N_SKIPPED_NOTOK} pending(no-output-yet)=${N_SKIPPED_NOFILE}"
} >> "${SELF_LOG}" 2>&1

# Keep the watcher's own log and state file bounded (small, but no reason to
# let them grow forever over months of cron runs).
for f in "${SELF_LOG}" "${STATE_FILE}"; do
    if [ -f "${f}" ]; then
        tail -n 20000 "${f}" > "${f}.tmp" 2>/dev/null && mv "${f}.tmp" "${f}"
    fi
done
