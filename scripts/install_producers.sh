#!/bin/bash
###############################################################################
# install_producers.sh — Install/refresh the vendored OMTF producer sources
#                          (../src/) into a CMSSW working area.
#
# Both packages below are vendored **in full** (not as diffs):
#   - L1Trigger/L1TMuonOverlapPhase1  — although a package of this name ships
#     in stock CMSSW, the version used by this analysis has diverged from it
#     extensively (base OMTF algorithm fork + local schema changes: ~40+ of
#     its 112 files differ from the stock CMSSW_14_2_0_pre2 release, including
#     new headers like FinalMuon.h/XmlIOCache.h that stock CMSSW doesn't have
#     at all). A diff-only overlay on top of a stock `git cms-addpkg` checkout
#     does NOT compile. We therefore ship and install the complete package.
#   - L1Trigger/L1MuNano              — fully custom package, does not exist
#     in stock CMSSW at all.
#
# See ../src/MANIFEST.md for the exact provenance and the
# producerSchemaVersion these correspond to.
#
# Usage:
#   bash install_producers.sh [/path/to/CMSSW_X_Y_Z]
#
# If no path is given, uses $CMSSW_BASE (must already be `cmsenv`'d).
# Safe to re-run: it overwrites the two vendored package directories in full.
###############################################################################
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_SRC="${SCRIPT_DIR}/../src"

CMSSW_TARGET="${1:-${CMSSW_BASE:-}}"
if [ -z "${CMSSW_TARGET}" ]; then
    echo "ERROR: no CMSSW area given and \$CMSSW_BASE is not set." >&2
    echo "Usage: bash install_producers.sh [/path/to/CMSSW_X_Y_Z]" >&2
    exit 1
fi

echo "=== Installing vendored OMTF producers ==="
echo "Repo source: ${REPO_SRC}"
echo "CMSSW area:  ${CMSSW_TARGET}"
echo ""

cd "${CMSSW_TARGET}/src"

# ---------------------------------------------------------------------------
# L1Trigger/L1TMuonOverlapPhase1 — vendored in full (see header comment above
# for why a diff-only overlay does not work: this is a forked/diverged
# version of the package, not the stock CMSSW one).
# ---------------------------------------------------------------------------
echo "Installing full L1Trigger/L1TMuonOverlapPhase1 package (forked from stock CMSSW)..."
rm -rf "L1Trigger/L1TMuonOverlapPhase1"
mkdir -p "L1Trigger/L1TMuonOverlapPhase1"
cp -r "${REPO_SRC}/L1Trigger/L1TMuonOverlapPhase1/." "L1Trigger/L1TMuonOverlapPhase1/"

# ---------------------------------------------------------------------------
# L1Trigger/L1MuNano — fully custom package, not part of upstream CMSSW.
# Vendor the entire tree.
# ---------------------------------------------------------------------------
echo "Installing full L1Trigger/L1MuNano package (fully custom, not upstream)..."
rm -rf "L1Trigger/L1MuNano"
mkdir -p "L1Trigger/L1MuNano"
cp -r "${REPO_SRC}/L1Trigger/L1MuNano/." "L1Trigger/L1MuNano/"

echo ""
echo "=== Producer install complete. Now build with: ==="
echo "  cd ${CMSSW_TARGET}/src"
echo "  scram b -j 8 L1Trigger/L1TMuonOverlapPhase1 L1Trigger/L1MuNano"

