#!/usr/bin/env bash
set -euo pipefail

# check_generated_event_chain.sh
#
# Run local emulator/ntuplizer smoke checks for generated-event configs,
# focused on B4 plus all G*_pos/G*_neg datasets.
#
# For each dataset:
# - runs condor/run_local_test.sh <dataset> <nevents> <procid>
# - checks OMTF trees in omtf_hits_*.root
# - checks Nano tables in omtf_nano_*.root for MuonStubTps/MuonStubKmtf/GenMuon
# - writes markdown summary report

BASEDIR="/afs/cern.ch/user/p/pleguina/omtf_hecin_dataset_production"
CONFDIR="${BASEDIR}/configs"
RUN_LOCAL="${BASEDIR}/condor/run_local_test.sh"

NEVENTS="${1:-5}"
PROCID="${2:-0}"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUTDIR="${BASEDIR}/smoke_results/generated_event_chain_${STAMP}"
REPORT="${OUTDIR}/GENERATED_EVENT_CHAIN_REPORT.md"

mkdir -p "${OUTDIR}"

# Build dataset list.
if [ "$#" -ge 3 ]; then
  DATASETS=("${@:3}")
else
  mapfile -t POS_NEG_DATASETS < <(
    cd "${CONFDIR}"
    ls G*_pos_cfg.py G*_neg_cfg.py 2>/dev/null \
      | sed 's/_cfg.py$//' \
      | sort -u
  )

  DATASETS=(B4)
  for ds in "${POS_NEG_DATASETS[@]}"; do
    DATASETS+=("${ds}")
  done
fi

cat > "${REPORT}" <<EOF
# Generated-Event Emulator/Ntuplizer Smoke Report

- Date: $(date)
- Events per dataset: ${NEVENTS}
- ProcId: ${PROCID}
- Scope: ${DATASETS[*]}

| Dataset | cmsRun | OMTFAllInputTree | OMTFHitsTree | MuonStubTps | MuonStubKmtf | GenMuon | Scratch |
|---|---|---|---|---|---|---|---|
EOF

for ds in "${DATASETS[@]}"; do
  echo "=== ${ds} ==="

  CMSRUN_STATUS="OK"
  OMTF_ALL="FAIL"
  OMTF_HITS="FAIL"
  TPS="FAIL"
  KMTF="FAIL"
  GENMUON="FAIL"

  if ! bash "${RUN_LOCAL}" "${ds}" "${NEVENTS}" "${PROCID}" > "${OUTDIR}/${ds}.log" 2>&1; then
    CMSRUN_STATUS="FAIL"
    SCRATCH="/tmp/${USER}_omtf_localtest_${ds}_${PROCID}"
    echo "| ${ds} | ${CMSRUN_STATUS} | ${OMTF_ALL} | ${OMTF_HITS} | ${TPS} | ${KMTF} | ${GENMUON} | ${SCRATCH} |" >> "${REPORT}"
    continue
  fi

  SCRATCH="/tmp/${USER}_omtf_localtest_${ds}_${PROCID}"
  OMTF_FILE="${SCRATCH}/omtf_hits_${ds}_${PROCID}_localtest.root"
  NANO_FILE="${SCRATCH}/omtf_nano_${ds}_${PROCID}_localtest.root"

  # Check OMTF trees
  python3 - <<PYEOF > "${OUTDIR}/${ds}_omtf_check.txt" 2>&1 || true
import ROOT
ROOT.gROOT.SetBatch(True)
f = ROOT.TFile.Open('${OMTF_FILE}')
if not f or f.IsZombie():
    print('OMTF_FILE_OPEN_FAIL')
    raise SystemExit(0)
d = f.Get('simOmtfPhase2Digis')
if not d:
    print('OMTF_DIR_FAIL')
    raise SystemExit(0)
print('OMTFAllInputTree', 'OK' if bool(d.Get('OMTFAllInputTree')) else 'FAIL')
print('OMTFHitsTree', 'OK' if bool(d.Get('OMTFHitsTree')) else 'FAIL')
PYEOF

  if grep -q 'OMTFAllInputTree OK' "${OUTDIR}/${ds}_omtf_check.txt"; then OMTF_ALL="OK"; fi
  if grep -q 'OMTFHitsTree OK' "${OUTDIR}/${ds}_omtf_check.txt"; then OMTF_HITS="OK"; fi

  # Check Nano branches/tables by name presence in Events branches.
  python3 - <<PYEOF > "${OUTDIR}/${ds}_nano_check.txt" 2>&1 || true
import ROOT
ROOT.gROOT.SetBatch(True)
try:
    f = ROOT.TFile.Open('${NANO_FILE}')
    if not f or f.IsZombie():
        raise RuntimeError('Could not open nano file')
    t = f.Get('Events')
    if not t:
        raise RuntimeError('Missing Events tree')
    branch_names = [b.GetName() for b in t.GetListOfBranches()]
    joined = '\n'.join(branch_names)
    print('MuonStubTps', 'OK' if 'MuonStubTps' in joined else 'FAIL')
    print('MuonStubKmtf', 'OK' if 'MuonStubKmtf' in joined else 'FAIL')
    print('GenMuon', 'OK' if 'GenMuon' in joined else 'FAIL')
except Exception:
    print('MuonStubTps FAIL')
    print('MuonStubKmtf FAIL')
    print('GenMuon FAIL')
PYEOF

  if grep -q 'MuonStubTps OK' "${OUTDIR}/${ds}_nano_check.txt"; then TPS="OK"; fi
  if grep -q 'MuonStubKmtf OK' "${OUTDIR}/${ds}_nano_check.txt"; then KMTF="OK"; fi
  if grep -q 'GenMuon OK' "${OUTDIR}/${ds}_nano_check.txt"; then GENMUON="OK"; fi

  echo "| ${ds} | ${CMSRUN_STATUS} | ${OMTF_ALL} | ${OMTF_HITS} | ${TPS} | ${KMTF} | ${GENMUON} | ${SCRATCH} |" >> "${REPORT}"
done

cat <<EOF

Generated report:
${REPORT}

Per-dataset logs:
${OUTDIR}
EOF
