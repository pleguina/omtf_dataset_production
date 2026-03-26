#!/bin/bash
# Real-time job monitoring for cluster 8672065

CLUSTER=8672065
TOTAL_JOBS=48
LOG_FILE="/afs/cern.ch/user/p/pleguina/omtf_hecin_dataset_production/logs/monitor_${CLUSTER}.log"

> "$LOG_FILE"  # Clear log

echo "[$(date)] Starting monitor for cluster $CLUSTER" >> "$LOG_FILE"
echo "Total jobs: $TOTAL_JOBS" >> "$LOG_FILE"
echo "" >> "$LOG_FILE"

while true; do
    # Extract idle and running from: "Total for query: 48 jobs; 0 completed, 0 removed, 1 idle, 47 running, 0 held, 0 suspended"
    QUERY_LINE=$(condor_q $CLUSTER 2>/dev/null | grep "^Total for query:")
    IDLE=$(echo "$QUERY_LINE" | sed 's/.*\([0-9]\+\) idle.*/\1/')
    RUNNING=$(echo "$QUERY_LINE" | sed 's/.*\([0-9]\+\) running.*/\1/')
    
    # Count jobs in history (completed/failed)
    HISTORY=$(condor_history $CLUSTER 2>/dev/null | grep "^$CLUSTER\." | wc -l)
    DONE=$(condor_history $CLUSTER 2>/dev/null -format "%s\n" ExitCode 2>/dev/null | grep -c "^0$" || echo 0)
    FAILED=$((HISTORY - DONE))
    
    PERCENT=$((100 * (HISTORY + 0) / TOTAL_JOBS))
    
    TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
    printf "[%s] ✓ %d/%d (%d%%) | 🏃 %s run | ⏳ %s idle | ❌ %d failed\n" \
        "$TIMESTAMP" "$HISTORY" "$TOTAL_JOBS" "$PERCENT" "$RUNNING" "$IDLE" "$FAILED" >> "$LOG_FILE"
    
    # Exit if all jobs finished
    if [[ $HISTORY -ge $TOTAL_JOBS ]]; then
        echo "[$(date)] ✅ Production complete: $DONE succeeded, $FAILED failed" >> "$LOG_FILE"
        break
    fi
    
    sleep 30
done
