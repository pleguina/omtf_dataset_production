#!/bin/bash
###############################################################################
# setup_cmssw.sh — Set up CMSSW working area for OMTF HECIN dataset production
###############################################################################
set -euo pipefail

BASEDIR="$HOME/omtf_hecin_dataset_production"
CMSSW_VERSION="CMSSW_14_2_0_pre2"
SCRAM_ARCH="el9_amd64_gcc12"

echo "=== OMTF HECIN Dataset Production — CMSSW Setup ==="
echo "Base directory: ${BASEDIR}"
echo "CMSSW version:  ${CMSSW_VERSION}"
echo "SCRAM_ARCH:     ${SCRAM_ARCH}"
echo ""

# Source CMS environment (set +u: cmsset_default.sh references VO_CMS_SW_DIR before setting it)
set +u
source /cvmfs/cms.cern.ch/cmsset_default.sh
set -u
export SCRAM_ARCH="${SCRAM_ARCH}"

cd "${BASEDIR}"

if [ -d "${CMSSW_VERSION}" ]; then
    echo "CMSSW area already exists. Entering..."
    cd "${CMSSW_VERSION}/src"
    set +u; eval "$(scramv1 runtime -sh)"; set -u
else
    echo "Creating CMSSW release area..."
    scramv1 project CMSSW "${CMSSW_VERSION}"
    cd "${CMSSW_VERSION}/src"
    set +u; eval "$(scramv1 runtime -sh)"; set -u
fi

# Create the generator fragment directory inside CMSSW src
FRAG_DIR="Configuration/GenProduction/python/OMTF_HECIN"
mkdir -p "${FRAG_DIR}"

# Copy fragments from our staging area
for frag in "${BASEDIR}/fragments/"*.py; do
    fname=$(basename "$frag")
    cp -v "$frag" "${FRAG_DIR}/${fname}"
done

# Create __init__.py only for the leaf package (OMTF_HECIN).
# Do NOT add __init__.py to intermediate directories (Configuration,
# GenProduction, python) — those are CMSSW namespace packages and adding
# __init__.py would shadow the release's Configuration.Eras etc. in Python 3.
touch "${FRAG_DIR}/__init__.py"

# ---------------------------------------------------------------------------
# Install EMTFTools/ParticleGuns — provides FlatRandomLLPGunProducer2 and
# related producers used by the displaced muon (D1) dataset.
#
# The emtftoolsparticleguns git submodule is not publicly accessible via git,
# so we copy the source tree from Carlos Vico Villalba's public AFS area.
# ---------------------------------------------------------------------------
EMTF_SRC="/afs/cern.ch/user/c/cvicovil/public/forPelayo/emtftoolsparticleguns"
EMTF_DIR="EMTFTools/ParticleGuns"

if [ -d "${EMTF_SRC}" ]; then
    echo "Installing EMTFTools/ParticleGuns from ${EMTF_SRC}..."
    mkdir -p "${EMTF_DIR}/interface" "${EMTF_DIR}/src" "${EMTF_DIR}/python"

    cp "${EMTF_SRC}/BuildFile.xml"                                 "${EMTF_DIR}/"
    find "${EMTF_SRC}/interface" -maxdepth 1 -name "*.h"  -exec cp {} "${EMTF_DIR}/interface/" \;
    find "${EMTF_SRC}/src"       -maxdepth 1 -name "*.cc" -exec cp {} "${EMTF_DIR}/src/" \;
    find "${EMTF_SRC}/python"    -maxdepth 1 -name "*.py" -exec cp {} "${EMTF_DIR}/python/" \;

    # The upstream SealModule.cc references FlatRandomHiggsGunProducer2 which
    # lives in a separate package (mc-tools/guns/plugins / GeneratorInterface).
    # Overwrite it with a version that only registers the four available producers.
    cat > "${EMTF_DIR}/src/SealModule.cc" <<'SEALEOF'
#include "FWCore/Framework/interface/MakerMacros.h"

#include "EMTFTools/ParticleGuns/interface/FlatRandomPtGunProducer2.h"
#include "EMTFTools/ParticleGuns/interface/FlatRandomLLPGunProducer2.h"
#include "EMTFTools/ParticleGuns/interface/FlatRandomBeamHaloGunProducer2.h"
#include "EMTFTools/ParticleGuns/interface/FlatRandomTau3MuGunProducer2.h"

using edm::FlatRandomPtGunProducer2;
using edm::FlatRandomLLPGunProducer2;
using edm::FlatRandomBeamHaloGunProducer2;
using edm::FlatRandomTau3MuGunProducer2;
DEFINE_FWK_MODULE(FlatRandomPtGunProducer2);
DEFINE_FWK_MODULE(FlatRandomLLPGunProducer2);
DEFINE_FWK_MODULE(FlatRandomBeamHaloGunProducer2);
DEFINE_FWK_MODULE(FlatRandomTau3MuGunProducer2);
SEALEOF

    # In CMSSW_14_2_0_pre2 the RandomNumberGenerator header moved from
    # FWCore/AbstractServices to FWCore/Utilities.  Patch all .cc files.
    find "${EMTF_DIR}/src" -name '*.cc' -exec \
        sed -i 's|FWCore/AbstractServices/interface/RandomNumberGenerator.h|FWCore/Utilities/interface/RandomNumberGenerator.h|g' {} \;

    echo "   -> EMTFTools/ParticleGuns installed"
else
    echo "[WARN] EMTF source not found at ${EMTF_SRC}"
    echo "       The displaced muon (D1) dataset requires this package."
    echo "       Ask Carlos Vico Villalba for access or copy the emtftoolsparticleguns"
    echo "       directory manually to: ${CMSSW_BASE}/src/${EMTF_DIR}"
fi

# Build
echo "Building CMSSW..."
scram b -j 8

# Refresh the local plugin cache so the framework can discover
# FlatRandomLLPGunProducer2 at runtime without needing a full rebuild.
echo "Refreshing edm plugin cache..."
edmPluginRefresh "${CMSSW_BASE}/lib/${SCRAM_ARCH}/"

echo ""
echo "=== CMSSW setup complete ==="
echo "CMSSW_BASE = ${CMSSW_BASE}"
