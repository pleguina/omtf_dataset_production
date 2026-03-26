#!/bin/bash
# Run testset_analysis.ipynb using the CMSSW Python that has ROOT.
# Outputs executed notebook + PDF plots into the same directory.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Load CMSSW environment (provides python3 with ROOT 6.30)
pushd "$HOME/CMSSW_14_2_0_pre2/src" > /dev/null
eval "$(scramv1 runtime -sh)"
popd > /dev/null

# Prevent ROOT/JupyROOT from hanging on missing X display
unset DISPLAY

echo "Python: $(which python3)"
echo "ROOT:   $(python3 -c 'import ROOT; print(ROOT.__version__)')"
echo ""
echo "Running testset_analysis.ipynb ..."

python3 -m nbconvert \
    --to notebook \
    --execute \
    --ExecutePreprocessor.timeout=600 \
    --ExecutePreprocessor.kernel_name=python3 \
    --output testset_analysis_executed.ipynb \
    testset_analysis.ipynb

echo ""
echo "Done! Output: $SCRIPT_DIR/testset_analysis_executed.ipynb"
echo "PDFs saved in: $SCRIPT_DIR/"
