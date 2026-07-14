#!/usr/bin/env bash
###############################################################################
# submit_das_validation.sh — generate Condor submit files and optionally
# submit production jobs for all first-validation DAS datasets.
#
# For each dataset this script:
#   1. Queries DAS for disk-accessible files (site filter per dataset).
#   2. Writes a per-dataset .sub file with event-chunked jobs.
#   3. If --submit is passed, calls condor_submit on each .sub file.
#
# Usage:
#   ./submit_das_validation.sh [--submit] [--max-files N] [--events-per-job N] [--max-events-per-dataset N] [--target PROFILE]
#
#   --submit        Actually submit to HTCondor (default: dry-run, write only)
#   --max-files N   Cap number of files per dataset (default: 0 = all disk files)
#   --events-per-job N           Hard cap on events processed by each job (default: 500)
#   --max-events-per-dataset N   Total events to process per dataset (default: 0 = all)
#   --target PROFILE             Apply per-dataset caps: minimum|good|strong
#                                Ignored when --max-events-per-dataset > 0.
#
# Outputs:
#   condor/das_<label>.sub  — one per dataset
###############################################################################
set -euo pipefail

SUBMIT=false
MAX_FILES=0
EVENTS_PER_JOB=500
MAX_EVENTS_PER_DATASET=0
TARGET_PROFILE="custom"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --submit)     SUBMIT=true; shift ;;
        --max-files)  MAX_FILES="${2}"; shift 2 ;;
        --events-per-job) EVENTS_PER_JOB="${2}"; shift 2 ;;
        --max-events-per-dataset) MAX_EVENTS_PER_DATASET="${2}"; shift 2 ;;
        --target) TARGET_PROFILE="${2}"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

case "${TARGET_PROFILE}" in
    custom|minimum|good|strong) ;;
    *)
        echo "ERROR: --target must be one of: minimum, good, strong"
        exit 1
        ;;
esac

get_target_events() {
    local label="$1"
    local profile="$2"

    case "${profile}" in
        minimum)
            case "${label}" in
                minbias) echo 200000 ;;
                displaced_lowpt|displaced_midpt|dy_prompt|llp_addon|single_muon_flatpt) echo 50000 ;;
                qcd_*|ttbar*) echo 50000 ;;
                *) echo 0 ;;
            esac
            ;;
        good)
            case "${label}" in
                minbias) echo 750000 ;;
                displaced_lowpt|displaced_midpt|single_muon_flatpt) echo 200000 ;;
                dy_prompt|llp_addon) echo 100000 ;;
                qcd_*|ttbar*) echo 100000 ;;
                *) echo 0 ;;
            esac
            ;;
        strong)
            case "${label}" in
                minbias) echo 3000000 ;;
                displaced_lowpt|displaced_midpt) echo 400000 ;;
                single_muon_flatpt) echo 500000 ;;
                dy_prompt) echo 300000 ;;
                llp_addon) echo 200000 ;;
                qcd_*|ttbar*) echo 200000 ;;
                *) echo 0 ;;
            esac
            ;;
        *)
            echo 0
            ;;
    esac
}

BASEDIR="/afs/cern.ch/user/p/pleguina/omtf_dataset_production"
CONDORDIR="${BASEDIR}/condor"
LOGBASE="${CONDORDIR}/logs"

export X509_USER_PROXY="/afs/cern.ch/user/p/pleguina/private/x509_proxy"

if ! command -v dasgoclient >/dev/null 2>&1; then
    echo "ERROR: dasgoclient not found — source CMSSW environment first"
    exit 1
fi

# label | global_tag | dataset_path | disk_site_filter
# site_filter: passed to "dasgoclient --query='file dataset=X site=Y'"
# Use empty string to query all sites (relies on global XRootD redirector).
SPECS=$(cat <<'EOF'
minbias|140X_mcRun4_realistic_v4|/MinBias_TuneCP5_14TeV-pythia8/Phase2Spring24DIGIRECOMiniAOD-PU140_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD|
displaced_lowpt|140X_mcRun4_realistic_v4|/DisplacedMuons_Pt-2To10_Dxy-0To3000-gun/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD|T2_DE_DESY
displaced_midpt|140X_mcRun4_realistic_v4|/DisplacedMuons_Pt-10To30_Dxy-0To3000-gun/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD|T2_CH_CERN
dy_prompt|140X_mcRun4_realistic_v4|/DYToLL_M-10To50_TuneCP5_14TeV-pythia8/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD|T1_IT_CNAF_Disk
llp_addon|140X_mcRun4_realistic_v4|/HTo2LongLivedTo4mu_MH-125_MFF-12_CTau-900mm_TuneCP5_14TeV-pythia8/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD|T2_DE_DESY
single_muon_flatpt|140X_mcRun4_realistic_v4|/SingleMu_FlatPt-2to100/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v2/GEN-SIM-DIGI-RAW-MINIAOD|T2_DE_DESY
EOF
)

mkdir -p "${LOGBASE}"

echo "=== DAS Validation Submit ==="
echo "Date       : $(date)"
echo "Submit     : ${SUBMIT}"
echo "Max files  : ${MAX_FILES} (0 = all)"
echo "Events/job : ${EVENTS_PER_JOB} (-1 = full file)"
echo "Max ev/ds  : ${MAX_EVENTS_PER_DATASET} (0 = all)"
echo "Target     : ${TARGET_PROFILE}"
echo ""

TOTAL_JOBS=0

while IFS='|' read -r label gt dataset site_filter; do
    [[ -z "${label}" || "${label}" == \#* ]] && continue

    echo "--- ${label} ---"
    echo "  Dataset : ${dataset}"
    echo "  GT      : ${gt}"
    echo "  Site    : ${site_filter}"

    # Query DAS for file list at preferred disk site.
    if [[ -n "${site_filter}" ]]; then
        DAS_QUERY="file dataset=${dataset} site=${site_filter}"
    else
        DAS_QUERY="file dataset=${dataset}"
    fi

    FILES=$(dasgoclient --query="${DAS_QUERY}" --limit=0 2>/dev/null | awk '{print $1}' | sort -u || true)

    if [[ -z "${FILES}" ]]; then
        echo "  [WARN] No files found for site=${site_filter}, falling back to all sites"
        FILES=$(dasgoclient --query="file dataset=${dataset}" --limit=0 2>/dev/null | awk '{print $1}' | sort -u || true)
    fi

    NFILES=$(echo "${FILES}" | grep -c '/store' || true)

    if [[ "${MAX_FILES}" -gt 0 && "${NFILES}" -gt "${MAX_FILES}" ]]; then
        FILES=$(echo "${FILES}" | awk -v max="${MAX_FILES}" 'NR<=max')
        NFILES="${MAX_FILES}"
    fi

    echo "  Files   : ${NFILES}"

    summary=$(dasgoclient --query="summary dataset=${dataset}" 2>/dev/null || true)
    nevents=$(echo "${summary}" | sed -n 's/.*nevents"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' | head -1)
    nfiles=$(echo "${summary}" | sed -n 's/.*nfiles"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' | head -1)
    if [[ -z "${nevents}" || -z "${nfiles}" || "${nfiles}" -le 0 ]]; then
        avg_events_per_file=1000
    else
        avg_events_per_file=$(( (nevents + nfiles - 1) / nfiles ))
    fi
    echo "  Avg ev/file (from DAS summary): ${avg_events_per_file}"

    SUB_FILE="${CONDORDIR}/das_${label}.sub"

    cat > "${SUB_FILE}" <<SUBHEADER
###############################################################################
# das_${label}.sub — HTCondor submit for DAS validation: ${label}
# Dataset: ${dataset}
# GT: ${gt}
# Generated: $(date)
###############################################################################
universe        = vanilla
executable      = ${CONDORDIR}/run_das_job.sh

output          = ${LOGBASE}/${label}_\$(ProcId).out
error           = ${LOGBASE}/${label}_\$(ProcId).err
log             = ${LOGBASE}/${label}.log

should_transfer_files = NO

request_cpus    = 1
request_memory  = 6000
request_disk    = 20000000

+JobFlavour     = "tomorrow"

+AccountingGroup = "group_u_CMS.u_zh.users"
use_x509userproxy = true

SUBHEADER

    PROFILE_TARGET=0
    if [[ "${MAX_EVENTS_PER_DATASET}" -eq 0 && "${TARGET_PROFILE}" != "custom" ]]; then
        PROFILE_TARGET=$(get_target_events "${label}" "${TARGET_PROFILE}")
    fi
    EFFECTIVE_MAX_EVENTS="${MAX_EVENTS_PER_DATASET}"
    if [[ "${EFFECTIVE_MAX_EVENTS}" -eq 0 ]]; then
        EFFECTIVE_MAX_EVENTS="${PROFILE_TARGET}"
    fi
    if [[ "${EFFECTIVE_MAX_EVENTS}" -gt 0 ]]; then
        echo "  Event cap: ${EFFECTIVE_MAX_EVENTS}"
    else
        echo "  Event cap: all"
    fi

    PROCID=0
    EVENTS_LEFT="${EFFECTIVE_MAX_EVENTS}"
    while IFS= read -r fpath; do
        [[ -z "${fpath}" ]] && continue

        if [[ "${EFFECTIVE_MAX_EVENTS}" -gt 0 && "${EVENTS_LEFT}" -le 0 ]]; then
            break
        fi

        if [[ "${EVENTS_PER_JOB}" -lt 0 ]]; then
            chunk_events=-1
            if [[ "${EFFECTIVE_MAX_EVENTS}" -gt 0 && "${EVENTS_LEFT}" -lt "${avg_events_per_file}" ]]; then
                chunk_events="${EVENTS_LEFT}"
            fi
            if [[ "${chunk_events}" -ne 0 ]]; then
                echo "arguments = ${label} ${PROCID} ${fpath} ${gt} 0 ${chunk_events}" >> "${SUB_FILE}"
                echo "queue" >> "${SUB_FILE}"
                PROCID=$(( PROCID + 1 ))
            fi
            if [[ "${EFFECTIVE_MAX_EVENTS}" -gt 0 ]]; then
                if [[ "${chunk_events}" -lt 0 ]]; then
                    EVENTS_LEFT=$(( EVENTS_LEFT - avg_events_per_file ))
                else
                    EVENTS_LEFT=$(( EVENTS_LEFT - chunk_events ))
                fi
            fi
            continue
        fi

        chunks_per_file=$(( (avg_events_per_file + EVENTS_PER_JOB - 1) / EVENTS_PER_JOB ))
        [[ "${chunks_per_file}" -lt 1 ]] && chunks_per_file=1

        skip=0
        chunk_idx=0
        while [[ "${chunk_idx}" -lt "${chunks_per_file}" ]]; do
            if [[ "${EFFECTIVE_MAX_EVENTS}" -gt 0 && "${EVENTS_LEFT}" -le 0 ]]; then
                break 2
            fi

            chunk_events="${EVENTS_PER_JOB}"
            remaining_in_file=$(( avg_events_per_file - skip ))
            if [[ "${remaining_in_file}" -lt "${chunk_events}" ]]; then
                chunk_events="${remaining_in_file}"
            fi
            if [[ "${EFFECTIVE_MAX_EVENTS}" -gt 0 && "${EVENTS_LEFT}" -lt "${chunk_events}" ]]; then
                chunk_events="${EVENTS_LEFT}"
            fi

            [[ "${chunk_events}" -le 0 ]] && break

            echo "arguments = ${label} ${PROCID} ${fpath} ${gt} ${skip} ${chunk_events}" >> "${SUB_FILE}"
            echo "queue" >> "${SUB_FILE}"
            PROCID=$(( PROCID + 1 ))
            skip=$(( skip + chunk_events ))
            chunk_idx=$(( chunk_idx + 1 ))

            if [[ "${EFFECTIVE_MAX_EVENTS}" -gt 0 ]]; then
                EVENTS_LEFT=$(( EVENTS_LEFT - chunk_events ))
            fi
        done
    done <<< "${FILES}"

    TOTAL_JOBS=$(( TOTAL_JOBS + PROCID ))
    echo "  Written : ${SUB_FILE} (${PROCID} jobs)"

    if [[ "${SUBMIT}" == "true" ]]; then
        condor_submit "${SUB_FILE}"
        echo "  Submitted ${PROCID} jobs for ${label}"
    fi

    echo ""

done <<< "${SPECS}"

echo "=== Summary ==="
echo "Total jobs : ${TOTAL_JOBS}"
if [[ "${SUBMIT}" == "false" ]]; then
    echo "Dry-run complete.  Re-run with --submit to submit."
fi
