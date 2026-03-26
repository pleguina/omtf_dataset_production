#!/bin/bash
# Simple one-shot status check

CLUSTER=8672065

echo "=== CLUSTER $CLUSTER STATUS ===" 
condor_q $CLUSTER
echo ""
echo "=== COMPLETED JOBS ===" 
condor_history $CLUSTER 2>/dev/null | grep "^$CLUSTER\." | wc -l
echo "jobs have finished"
echo ""
condor_history $CLUSTER 2>/dev/null -format "%s\n" ExitCode | sort | uniq -c
