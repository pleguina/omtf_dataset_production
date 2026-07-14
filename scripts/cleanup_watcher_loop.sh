#!/bin/bash
###############################################################################
# cleanup_watcher_loop.sh — persistent wrapper around cleanup_condor_logs.sh.
#
# WHY THIS EXISTS (and not crontab):
#   Personal crontab is disabled on lxplus for this account:
#     "You (pleguina) are not allowed to use this program (crontab)"
#   CERN's alternative (cron.cern.ch / ccrontab) is not available/configured
#   on this node either. Also, plain `nohup ... &` from an interactive lxplus
#   shell does NOT reliably survive logout: lxplus.cern.ch round-robins across
#   many physical nodes, so logging back in very likely lands you on a
#   DIFFERENT node than the one your nohup'd process is running on — from
#   there it is invisible and effectively unmanageable.
#
#   HTCondor itself is the one thing here that is guaranteed to keep running
#   independent of the interactive login node: the schedd is a persistent
#   service, not tied to any ssh session. So the cleanup watcher is submitted
#   as a normal (lightweight) HTCondor job — see condor/cleanup_watcher.sub —
#   which loops calling cleanup_condor_logs.sh every SLEEP_SECONDS.
#
# TERMINATION CONDITION ("works until there are condor jobs available"):
#   Every pass, after cleaning, the loop checks condor_q for this user,
#   EXCLUDING its own ClusterId (otherwise it would see itself and never
#   consider the queue "empty"). As soon as no OTHER job for this user is
#   queued/running/held, it runs one final cleanup pass (to catch whatever
#   just finished) and exits — the condor job then completes on its own.
#   A generous MAX_ITERATIONS safety cap still exists in case condor_q
#   itself becomes unavailable/broken for a long time, so this never turns
#   into a truly infinite job.
#
# Usage: cleanup_watcher_loop.sh [SELF_CLUSTER_ID]
#   SELF_CLUSTER_ID — this job's own $(ClusterId), passed automatically via
#   `arguments = $(ClusterId)` in condor/cleanup_watcher.sub, so the loop can
#   exclude itself when deciding whether the queue is "empty". If omitted
#   (e.g. manual testing), the loop falls back to the fixed-iteration cap.
###############################################################################
set -uo pipefail

SLEEP_SECONDS="${CLEANUP_SLEEP_SECONDS:-300}"      # 5 minutes between passes
MAX_ITERATIONS="${CLEANUP_MAX_ITERATIONS:-12000}"  # safety cap only (~41 days @5min)
SELF_CLUSTER="${1:-}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLEANUP_SCRIPT="${SCRIPT_DIR}/cleanup_condor_logs.sh"

echo "=== cleanup_watcher_loop starting on $(hostname -f) at $(date) ==="
echo "sleep_seconds=${SLEEP_SECONDS} max_iterations=${MAX_ITERATIONS} self_cluster=${SELF_CLUSTER:-<none>}"

# Count jobs for this user still in the queue (idle/running/held), excluding
# our own ClusterId if known. condor_q only lists active jobs (not completed
# or removed ones), so this is exactly "is there still production work out there".
other_jobs_remaining() {
    local rows
    rows=$(timeout -k 5 30 condor_q "${USER}" -af ClusterId 2>/dev/null) || { echo 1; return; }
    if [ -z "${SELF_CLUSTER}" ]; then
        echo "${rows}" | grep -c . || true
    else
        echo "${rows}" | grep -v -x "${SELF_CLUSTER}" | grep -c . || true
    fi
}

i=0
while [ "${i}" -lt "${MAX_ITERATIONS}" ]; do
    i=$((i + 1))
    echo "--- iteration ${i}/${MAX_ITERATIONS} at $(date) ---"
    bash "${CLEANUP_SCRIPT}" || echo "WARNING: cleanup_condor_logs.sh exited non-zero (continuing loop)"

    if [ -n "${SELF_CLUSTER}" ]; then
        N=$(other_jobs_remaining)
        echo "other jobs for ${USER} still queued/running/held: ${N}"
        if [ "${N}" -eq 0 ]; then
            echo "no more production jobs left in the queue — running one final cleanup pass and exiting"
            sleep 30
            bash "${CLEANUP_SCRIPT}" || true
            echo "=== cleanup_watcher_loop: queue empty, exiting at $(date) ==="
            exit 0
        fi
    fi

    sleep "${SLEEP_SECONDS}"
done

echo "=== cleanup_watcher_loop reached MAX_ITERATIONS (safety cap), exiting at $(date) ==="
echo "If production is still ongoing, just resubmit condor/cleanup_watcher.sub."
