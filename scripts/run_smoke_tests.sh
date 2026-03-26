#!/bin/bash
# run_smoke_tests.sh — Run 50-event smoke tests for all datasets from configs/
#
# Usage (from any directory, after cmsenv):
#   bash ~/omtf_hecin_dataset_production/scripts/run_smoke_tests.sh
#
set -uo pipefail

BASEDIR="$HOME/omtf_hecin_dataset_production"
CONFIGS="$BASEDIR/configs"
OUTDIR="$BASEDIR/smoke_results"
NEVENTS=10

mkdir -p "$OUTDIR"
cd "$OUTDIR"

# Copy customize_omtf_dumper.py here so cmsRun can find it on import
cp "$CONFIGS/customize_omtf_dumper.py" .

DATASETS=(S1 S2 S3 S4 S5 B1 B2 B3)
RESULTS=()

for DS in "${DATASETS[@]}"; do
    echo ""
    echo "=============================="
    echo "  Running $DS (${NEVENTS} events)"
    echo "=============================="

    # Patch config: fewer events + single thread to avoid MT TFileService race
    python3 - <<PYEOF
import re, sys
txt = open('$CONFIGS/${DS}_cfg.py').read()
txt = txt.replace('input = cms.untracked.int32(100)', 'input = cms.untracked.int32($NEVENTS)')
txt = txt.replace('process.options.numberOfThreads = 4', 'process.options.numberOfThreads = 1')
txt = txt.replace('process.options.numberOfStreams = 0', 'process.options.numberOfStreams = 1')
# Write RAWSIM pool to a real temp file (ROOT cannot fdatasync /dev/null)
txt = txt.replace("file:${DS}.root", "file:${DS}_pool_tmp.root")
open('${DS}_smoke_cfg.py', 'w').write(txt)
print("  Patched ${DS}_smoke_cfg.py")
PYEOF

    # Run
    rm -f omtf_hits.root
    cmsRun "${DS}_smoke_cfg.py" > "${DS}_smoke.log" 2>&1 || true
    EXIT=$?

    # Rename output and clean up pool file
    mv omtf_hits.root "omtf_hits_${DS}.root" 2>/dev/null || true
    rm -f "${DS}_pool_tmp.root"

    # Quick check
    if [ -f "omtf_hits_${DS}.root" ]; then
        SUMMARY=$(python3 - <<PYEOF2
import ROOT, sys
ROOT.gROOT.SetBatch(True)
f = ROOT.TFile("omtf_hits_${DS}.root")
d = f.Get("simOmtfPhase2Digis")
if not d:
    print("  MISSING simOmtfPhase2Digis directory")
    sys.exit(1)
t1 = d.Get("OMTFAllInputTree")
t2 = d.Get("OMTFHitsTree")
n1 = t1.GetEntries() if t1 else -1
n2 = t2.GetEntries() if t2 else -1
print(f"  OMTFAllInputTree={n1}  OMTFHitsTree={n2}")
PYEOF2
)
        echo "$SUMMARY"
        RESULTS+=("$DS: exit=$EXIT $SUMMARY")
    else
        echo "  ERROR: omtf_hits_${DS}.root not produced"
        RESULTS+=("$DS: exit=$EXIT  ROOT FILE MISSING")
    fi
done

echo ""
echo "=============================="
echo "  SMOKE TEST SUMMARY"
echo "=============================="
for R in "${RESULTS[@]}"; do
    echo "  $R"
done
echo ""
echo "Logs and ROOT files in: $OUTDIR"
