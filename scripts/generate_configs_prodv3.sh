#!/bin/bash
###############################################################################
# generate_configs_prodv3.sh — cmsDriver configs for prodv3 (CMSSW_20_0_0_patch2)
#
# Replays the cmsDriver command of every prodv2 config in configs/ with the
# prodv3 settings and writes the result to configs_prodv3/:
#   geometry  ExtendedRun4D127 (prodv2: Extended2026D110)
#   era       Phase2C26I13M9   (prodv2: Phase2C17I13M9)
#   GT        200X_mcRun4_realistic_v1 (prodv2: 140X_mcRun4_realistic_v4)
#   pileup    Phase2Fall26GS MinBias (made with CMSSW_20_0_0_patch2), a fixed
#             3000-file subset of the 18175 files, all at T2_CH_CERN
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
OUTDIR="${BASEDIR}/configs_prodv3"
GEOMETRY="ExtendedRun4D127"
ERA="Phase2C26I13M9"
CONDITIONS="200X_mcRun4_realistic_v1"
PU_LIST="${OUTDIR}/pu_filelist_Phase2Fall26GS_MinBias.txt"

if [ -z "${CMSSW_BASE:-}" ] || [[ "${CMSSW_VERSION:-}" != CMSSW_20_0_0* ]]; then
    echo "ERROR: run inside the CMSSW_20_0_0_patch2 environment (cmsenv)"; exit 1
fi

mkdir -p "${OUTDIR}"
cp "${SRCDIR}/customize_omtf_dumper.py" "${OUTDIR}/"

# Pileup file list (made once with dasgoclient, kept in the repo for reproducibility):
#   dasgoclient -query "file dataset=/MinBias_TuneCP5_14TeV-pythia8/Phase2Fall26GS-minbias_200X_mcRun4_realistic_v1-v1/GEN-SIM site=T2_CH_CERN" \
#     | sort | awk 'NR%6==1' | head -3000
if [ ! -s "${PU_LIST}" ]; then echo "ERROR: missing pileup list ${PU_LIST}"; exit 1; fi

# prodv3-only datasets: <name>:<prodv2 config whose cmsDriver command is reused with the fragment swapped>
EXTRA_DATASETS="D2_llp_overlap_PU200:C22_disp_pt20to50_overlap_PU200"

cd "${OUTDIR}"
for cfg in "${SRCDIR}"/*_cfg.py ${EXTRA_DATASETS}; do
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
echo "=== prodv3 configs in ${OUTDIR} ==="
