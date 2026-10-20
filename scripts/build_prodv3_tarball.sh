#!/bin/bash
# build_prodv3_tarball.sh -- pack a CMSSW area + job configs into one tarball on EOS (prodv3 defaults; see env below).
# prodv4: OMTF_CMSSW_VERSION=CMSSW_20_0_0_patch2_prodv4 OMTF_CONFIGS_DIR=configs_prodv4 \
#         OMTF_RUN_SCRIPTS="run_job_prodv4.sh run_das_job_prodv3.sh" OMTF_TARBALL_EOS=/eos/user/p/pleguina/condor/omtf_prodv4/cmssw_prodv4.tar.gz
# Batch jobs fetch it with transfer_input_files (xrootd plugin) and unpack it in their scratch
# directory, so they never read CMSSW libraries, python or configs from AFS.
# Usage: build_prodv3_tarball.sh [STAGE_DIR]   (STAGE_DIR must be on local disk, not AFS)
set -euo pipefail
CMSSW_VERSION="${OMTF_CMSSW_VERSION:-CMSSW_20_0_0_patch2}"
CMSSW_DIR="$HOME/${CMSSW_VERSION}"
BASEDIR="$HOME/omtf_dataset_production"
EOS_TAR="${OMTF_TARBALL_EOS:-/eos/user/p/pleguina/condor/omtf_prodv3/cmssw_prodv3.tar.gz}"
STAGE="${1:-$(mktemp -d /tmp/${USER}_prodv3_tar.XXXXXX)}"
case "$STAGE" in /afs/*) echo "STAGE_DIR must not be on AFS"; exit 1;; esac
mkdir -p "$STAGE/payload"

_git_id() { local h; h="$(git -C "$1" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)";
            [ -n "$(git -C "$1" status --porcelain --untracked-files=no 2>/dev/null)" ] && h="${h}+dirty"; echo "$h"; }
echo "cmssw:$(_git_id "${CMSSW_DIR}/src");prod:$(_git_id "${BASEDIR}")" > "$STAGE/payload/producer_commit.txt"
CONFIGS_DIR="${OMTF_CONFIGS_DIR:-configs_prodv3}"
for d in ${CONFIGS_DIR}; do rm -rf "$STAGE/payload/${d}"; cp -r "${BASEDIR}/${d}" "$STAGE/payload/${d}"; done   # one or more dirs (space-separated)
for f in ${OMTF_RUN_SCRIPTS:-run_job_prodv3.sh run_das_job_prodv3.sh}; do cp "${BASEDIR}/condor/$f" "$STAGE/payload/" 2>/dev/null || true; done

tar -C "$HOME" -czf "$STAGE/cmssw_prodv3.tar.gz" \
    --exclude="${CMSSW_VERSION}/tmp" --exclude="${CMSSW_VERSION}/logs" --exclude='.git' \
    --exclude="${CMSSW_VERSION}/*.log" "${CMSSW_VERSION}" \
    -C "$STAGE" payload
ls -lh "$STAGE/cmssw_prodv3.tar.gz"
eos mkdir -p "$(dirname "$EOS_TAR")" 2>/dev/null || true
xrdcp --force "$STAGE/cmssw_prodv3.tar.gz" "root://eosuser.cern.ch/${EOS_TAR}"
echo "uploaded ${EOS_TAR} ($(cat "$STAGE/payload/producer_commit.txt"))"
