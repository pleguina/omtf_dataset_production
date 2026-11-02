#!/bin/bash
###############################################################################
# generate_configs_prodv4.sh — cmsDriver configs for prodv4 (CMSSW_20_0_0_patch2_prodv4 area)
#
# Replays the cmsDriver command of a template prodv2 config in configs/ with the
# prodv4 settings (same as prodv3) and writes the result to configs_prodv4/:
#   geometry  ExtendedRun4D127 (prodv2: Extended2026D110)
#   era       Phase2C26I13M9   (prodv2: Phase2C17I13M9)
#   GT        200X_mcRun4_realistic_v1 (prodv2: 140X_mcRun4_realistic_v4)
#   pileup    Phase2Fall26GS MinBias (made with CMSSW_20_0_0_patch2), a fixed
#             3000-file subset of the 18175 files (every 6th file starting at the 4th: disjoint from the
#             prodv3 subset, which started at the 1st), all at T2_CH_CERN
#
# Why not D110: CMSSW_20_0_X moved the SimDataFormats classes to the io_v1
# namespace and cannot read files written before it (DictionaryNotFound for
# std::vector<PSimHit>, SimTrack, GenEventInfoProduct, ...), so the prodv2
# Phase2Spring24GS pileup and the Phase2Spring24 RAW samples are unreadable.
#
# Usage (inside the CMSSW_20_0_0_patch2 environment):
#   scripts/generate_configs_prodv3.sh            # all datasets
#   scripts/generate_configs_prodv3.sh C10 C22 B4 # only datasets whose name starts with these
###############################################################################
set -euo pipefail

BASEDIR="/afs/cern.ch/user/${USER:0:1}/${USER}/omtf_dataset_production"
SRCDIR="${BASEDIR}/configs"
OUTDIR="${BASEDIR}/configs_prodv4"
GEOMETRY="ExtendedRun4D127"
ERA="Phase2C26I13M9"
CONDITIONS="200X_mcRun4_realistic_v1"
PU_LIST="${OUTDIR}/pu_filelist_Phase2Fall26GS_MinBias.txt"

if [ -z "${CMSSW_BASE:-}" ] || [[ "${CMSSW_VERSION:-}" != CMSSW_20_0_0* ]]; then
    echo "ERROR: run inside the prodv4 CMSSW area environment (cmsenv)"; exit 1
fi

mkdir -p "${OUTDIR}"
# the prodv4 customiser is kept in configs_prodv4/ (edit it there; it adds the schema-metadata dataset identity)
[ -s "${OUTDIR}/customize_omtf_dumper.py" ] || cp "${BASEDIR}/configs_prodv3/customize_omtf_dumper.py" "${OUTDIR}/"

# Pileup file list (made once with dasgoclient, kept in the repo for reproducibility):
#   dasgoclient -query "file dataset=/MinBias_TuneCP5_14TeV-pythia8/Phase2Fall26GS-minbias_200X_mcRun4_realistic_v1-v1/GEN-SIM site=T2_CH_CERN" \
#     | sort | awk 'NR%6==1' | head -3000
if [ ! -s "${PU_LIST}" ]; then echo "ERROR: missing pileup list ${PU_LIST}"; exit 1; fi

# prodv4 datasets: <name>:<prodv2 config whose cmsDriver command is reused with the fragment swapped>
# (V*/D3 fragments come from scripts/make_prodv4_fragments.py; C13-C16 and B4 reuse their own prodv2 command)
EXTRA_DATASETS="V1_vdisp_pt10to20_overlap_PU200:C22_disp_pt20to50_overlap_PU200 V2_vdisp_pt20to50_overlap_PU200:C22_disp_pt20to50_overlap_PU200 V3_vdisp_pt50to100_overlap_PU200:C22_disp_pt20to50_overlap_PU200 V4_vdisp_pt10to20_overlap:C10_disp_pt20to50_overlap V5_vdisp_pt20to50_overlap:C10_disp_pt20to50_overlap V6_vdisp_pt50to100_overlap:C10_disp_pt20to50_overlap"
for b in 0 1 2 3; do for m in 15 50; do for ct in 100 1000; do
    EXTRA_DATASETS="${EXTRA_DATASETS} D3_llp_b${b}_m${m}_ct${ct}_PU200:C22_disp_pt20to50_overlap_PU200"
done; done; done
BASE_DATASETS="C13_prompt_pt2to5_overlap_PU200 C14_prompt_pt5to10_overlap_PU200 C15_disp_pt2to5_overlap_PU200 C16_disp_pt5to10_overlap_PU200 B4"

# Test mode: OMTF_TEST_CONFIGS=1 writes only the smoke-test configs (T*, no PU) to configs_prodv4test/
if [ -n "${OMTF_TEST_CONFIGS:-}" ]; then
    OUTDIR="${BASEDIR}/configs_prodv4test"; mkdir -p "${OUTDIR}"
    [ -s "${OUTDIR}/customize_omtf_dumper.py" ] || cp "${BASEDIR}/configs_prodv4/customize_omtf_dumper.py" "${OUTDIR}/"
    BASE_DATASETS=""
    EXTRA_DATASETS="T1_llp_b1_nopu:C10_disp_pt20to50_overlap T2_llp_b1_nopu_zeroT:C10_disp_pt20to50_overlap"
fi
cd "${OUTDIR}"
for cfg in $(for n in ${BASE_DATASETS}; do echo "${SRCDIR}/${n}_cfg.py"; done) ${EXTRA_DATASETS}; do
    template=""
    if [[ "${cfg}" == *:* ]]; then
        template="${cfg#*:}"; name="${cfg%%:*}"; cfg="${SRCDIR}/${template}_cfg.py"
    else
        name="$(basename "${cfg}" _cfg.py)"
    fi
    [ "${name}" = "das_reemul" ] && continue
    if [ $# -gt 0 ]; then
        keep=0; for p in "$@"; do [[ "${name}" == "${p}"* ]] && keep=1; done
        [ ${keep} -eq 1 ] || continue
    fi
    cmdline="$(grep -m1 '^# with command line options:' "${cfg}" | sed 's/^# with command line options: //')"
    [ -n "${template}" ] && cmdline="${cmdline/OMTF_HECIN\/${template}.py/OMTF_HECIN\/${name}.py}"
    if [ -z "${cmdline}" ]; then echo "SKIP ${name}: no cmsDriver command line"; continue; fi

    # Rebuild the argument list with the prodv3 settings.
    args="$(python3 - "${cmdline}" "${name}" "${GEOMETRY}" "${ERA}" "${CONDITIONS}" "${PU_LIST}" <<'EOF'
import shlex, sys
cmd, name, geom, era, gt, pulist = sys.argv[1:]
a = shlex.split(cmd)
out, i = [a[0]], 1
over = {"--python_filename": f"{name}_cfg.py", "--fileout": f"file:{name}.root", "--geometry": geom, "--era": era, "--conditions": gt,
        "--customise_commands": "process.trackerGeometry.applyAlignment=cms.bool(False)"}
seen = set()
while i < len(a):
    k = a[i]
    if k.startswith("--") and i + 1 < len(a) and not a[i + 1].startswith("--"):
        v = a[i + 1]; i += 2
    else:
        v = None; i += 1
    if k in over:
        v = over[k]; seen.add(k)
    elif k == "--pileup_input":
        v = f"filelist:{pulist}"
    out += [k] + ([v] if v is not None else [])
for k in over:
    if k not in seen:
        out += [k, over[k]]
print(shlex.join(out))
EOF
)"
    echo "--- ${name}"
    eval cmsDriver.py ${args} > "${name}.cmsDriver.log" 2>&1 || { echo "ERROR: cmsDriver failed for ${name} (see ${OUTDIR}/${name}.cmsDriver.log)"; continue; }
    cat >> "${name}_cfg.py" <<'EOF'

# ===== OMTF DataROOTDumper2 =====
from customize_omtf_dumper import customise_omtf_dumper
process = customise_omtf_dumper(process)
EOF
    rm -f "${name}.cmsDriver.log"
done
# Multi-BX variants of the m50 / ct1000 LLP bins: same config, different dataset name (the OMTF BX window is set by
# OMTF_BX_RANGE in the submit file, not in the config)
for b in 0 1 2 3; do
    if [ -f "D3_llp_b${b}_m50_ct1000_PU200_cfg.py" ]; then
        sed "s#D3_llp_b${b}_m50_ct1000_PU200#D4_llp_b${b}_m50_ct1000_PU200_multibx#g" "D3_llp_b${b}_m50_ct1000_PU200_cfg.py" > "D4_llp_b${b}_m50_ct1000_PU200_multibx_cfg.py"
    fi
done
# Multi-BX rate sample: B4 config under a new dataset name (new run number and seeds; BX window from OMTF_BX_RANGE)
if [ -f "B4_cfg.py" ]; then
    sed "s#file:B4.root#file:B4_multibx.root#" "B4_cfg.py" > "B4_multibx_cfg.py"
fi
echo "=== prodv4 configs in ${OUTDIR} ==="
