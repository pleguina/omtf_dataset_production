#!/usr/bin/env bash
###############################################################################
# run_manifest_smoke_matrix.sh — local smoke-test + audit every locally
# generatable (non-DAS) manifest-authorized dataset.
#
# Rationale: full-scale condor production is NOT required to validate schema/
# structural correctness (event-join closure, truth unification, PU
# provenance, stub-vector closure, forbidden-truth-variable guard). A small
# local cmsRun (default 30 events) using condor/run_local_test.sh is
# sufficient, since these audits check structural properties that manifest
# at any statistics level.
#
# Skips:
#   - family == "DAS"            (requires xrootd read via a valid grid proxy)
#   - roles == ["historical_excluded"] with no resolvable configs/*_cfg.py
#     (these predate the current campaign and are out of scope)
#
# Usage: ./run_manifest_smoke_matrix.sh [NEVENTS] [--only NAME1,NAME2,...]
###############################################################################
set -uo pipefail

BASEDIR="/afs/cern.ch/user/${USER:0:1}/${USER}/omtf_dataset_production"
cd "${BASEDIR}"

NEVENTS="${1:-30}"
ONLY_FILTER="${3:-}"
if [[ "${2:-}" == "--only" ]]; then
  ONLY_FILTER="${3:-}"
fi

OUTROOT="${BASEDIR}/smoke_results/manifest_smoke"
mkdir -p "${OUTROOT}"
SUMMARY="${OUTROOT}/SUMMARY.jsonl"

echo "=== Manifest smoke matrix starting $(date) — NEVENTS=${NEVENTS} ==="

# Build the worklist: name<TAB>config_basename for every non-DAS entry with a
# resolvable configs/<basename>_cfg.py.
WORKLIST=$(python3 - <<'PYEOF'
import json, os

m = json.load(open("dataset_manifest.json"))
for d in m["authorizedDatasets"]:
    if d.get("family") == "DAS":
        continue
    name = d["name"]
    candidates = [name]
    submit = d.get("submit") or ""
    if submit.startswith("condor/") and submit.endswith(".sub"):
        candidates.append(os.path.basename(submit)[:-4])
    resolved = None
    for c in candidates:
        if os.path.exists(f"configs/{c}_cfg.py"):
            resolved = c
            break
    if resolved:
        print(f"{name}\t{resolved}")
PYEOF
)

TOTAL=$(echo "${WORKLIST}" | grep -c . || true)
echo "Worklist: ${TOTAL} locally-generatable datasets to smoke-test."

N=0
while IFS=$'\t' read -r NAME CFGBASE; do
  [[ -z "${NAME}" ]] && continue
  if [[ -n "${ONLY_FILTER}" ]] && [[ ",${ONLY_FILTER}," != *",${NAME},"* ]]; then
    continue
  fi
  N=$((N+1))
  echo ""
  echo "=== [${N}/${TOTAL}] ${NAME} (config=${CFGBASE}) $(date) ==="

  PROCID=0
  SCRATCH="/tmp/${USER}_omtf_localtest_${CFGBASE}_${PROCID}"
  DSDIR="${OUTROOT}/${NAME}"
  mkdir -p "${DSDIR}"

  ( condor/run_local_test.sh "${CFGBASE}" "${NEVENTS}" "${PROCID}" ) \
    > "${DSDIR}/run_local_test.log" 2>&1
  CMSRUN_RC=$?

  HITS="${SCRATCH}/omtf_hits_${CFGBASE}_${PROCID}_localtest.root"
  NANO="${SCRATCH}/omtf_nano_${CFGBASE}_${PROCID}_localtest.root"

  AUDIT_STATUS="skipped_cmsrun_failed"
  if [[ ${CMSRUN_RC} -eq 0 && -f "${HITS}" ]]; then
    cp "${HITS}" "${DSDIR}/" 2>/dev/null || true
    [[ -f "${NANO}" ]] && cp "${NANO}" "${DSDIR}/" 2>/dev/null || true

    NANO_ARG=()
    [[ -f "${NANO}" ]] && NANO_ARG=(--nano "${NANO}")

    python3 scripts/audit/audit_full_campaign.py \
      --manifest dataset_manifest.json \
      --hits "${HITS}" \
      "${NANO_ARG[@]}" \
      --outdir "${DSDIR}/full_campaign_audit" \
      > "${DSDIR}/full_campaign_audit.log" 2>&1
    if [[ $? -eq 0 ]]; then
      AUDIT_STATUS="pass"
    else
      AUDIT_STATUS="fail"
    fi
  fi

  python3 - "${NAME}" "${CFGBASE}" "${CMSRUN_RC}" "${AUDIT_STATUS}" >> "${SUMMARY}" <<'PYEOF'
import json, sys, datetime
name, cfgbase, cmsrun_rc, audit_status = sys.argv[1:5]
print(json.dumps({
    "name": name,
    "config": cfgbase,
    "cmsrun_exit_code": int(cmsrun_rc),
    "audit_status": audit_status,
    "timestamp": datetime.datetime.now().isoformat(),
}))
PYEOF

  echo "=== [${N}/${TOTAL}] ${NAME}: cmsrun_rc=${CMSRUN_RC} audit=${AUDIT_STATUS} ==="

  # Free scratch space once copied.
  rm -rf "${SCRATCH}" 2>/dev/null || true

done <<< "${WORKLIST}"

echo ""
echo "=== Manifest smoke matrix finished $(date) ==="
echo "Summary: ${SUMMARY}"
