# das_reemul_cfg.py
#
# Phase-2 L1 re-emulation config for DAS-sourced GEN-SIM-DIGI-RAW-MINIAOD samples.
# Runs SimL1Emulator (incl. Phase-2 OMTF) + OMTF ROOT/NanoAOD dumper.
#
# Required env vars:
#   DAS_INPUT_FILE   - XRootD path (root://...) or /store/mc/... (auto-prefixed)
#   DAS_GLOBAL_TAG   - Global Tag, e.g. 140X_mcRun4_realistic_v4
#   DAS_OMTF_OUTPUT  - TFileService output filename (e.g. omtf_hits_label.root)
#
# Optional env vars:
#   DAS_NANO_OUTPUT  - NanoAOD output filename (default: omtf_nano_das.root)
#   DAS_MAX_EVENTS   - integer, default -1 (all events in file)
#   DAS_SKIP_EVENTS  - integer, default 0 (for chunked processing)
#
# The config is shared by all DAS validation datasets; only the env vars change
# between datasets.  The run_das_smoke_test.sh and run_das_job.sh scripts
# populate these variables.

import FWCore.ParameterSet.Config as cms
import os

from Configuration.Eras.Era_Phase2C17I13M9_cff import Phase2C17I13M9

process = cms.Process('L1REEMUL', Phase2C17I13M9)

# --- Services ---
process.load('Configuration.StandardSequences.Services_cff')
process.load('FWCore.MessageService.MessageLogger_cfi')
process.MessageLogger.cerr.FwkReport.reportEvery = cms.untracked.int32(1000)

# --- Geometry ---
process.load('Configuration.Geometry.GeometryExtended2026D110Reco_cff')
process.load('Configuration.Geometry.GeometryExtended2026D110_cff')

# --- Magnetic field ---
process.load('Configuration.StandardSequences.MagneticField_cff')

# --- Mixing setup (provides HGCAL/HFNose noise PSets used by L1 HGCal chain) ---
process.load('SimGeneral.MixingModule.mixNoPU_cfi')

# --- L1 emulator (includes Phase-2 OMTF: simOmtfPhase2Digis) ---
process.load('Configuration.StandardSequences.SimL1Emulator_cff')

# --- End-of-process / Global Tag ---
process.load('Configuration.StandardSequences.EndOfProcess_cff')
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')

from Configuration.AlCa.GlobalTag import GlobalTag
_gt = os.environ.get('DAS_GLOBAL_TAG', '140X_mcRun4_realistic_v4')
process.GlobalTag = GlobalTag(process.GlobalTag, _gt, '')

# Tracker alignment payload has 43708 modules; CMSSW_14_2_0_pre2 geometry has
# 43600.  Safe to disable for L1 studies (no tracker hit reconstruction done).
if hasattr(process, 'trackerGeometry'):
    process.trackerGeometry.applyAlignment = cms.bool(False)

# --- Input ---
_input_file = os.environ.get('DAS_INPUT_FILE', '')
if not _input_file:
    raise RuntimeError("DAS_INPUT_FILE env var must be set")
if not _input_file.startswith('root://') and not _input_file.startswith('file:'):
    _input_file = 'root://cms-xrd-global.cern.ch/' + _input_file

process.source = cms.Source("PoolSource",
    fileNames=cms.untracked.vstring(_input_file),
)

_skip_events = int(os.environ.get('DAS_SKIP_EVENTS', '0'))
if _skip_events > 0:
    process.source.skipEvents = cms.untracked.uint32(_skip_events)

# --- Max events ---
process.maxEvents = cms.untracked.PSet(
    input=cms.untracked.int32(int(os.environ.get('DAS_MAX_EVENTS', '-1'))),
)

# --- Options ---
process.options = cms.untracked.PSet(
    numberOfThreads=cms.untracked.uint32(1),
    numberOfStreams=cms.untracked.uint32(1),
    wantSummary=cms.untracked.bool(False),
)

# --- TFileService ---
process.TFileService = cms.Service("TFileService",
    fileName=cms.string(os.environ.get('DAS_OMTF_OUTPUT', 'omtf_hits_das.root')),
)

# --- Initial schedule: L1 re-emulation only ---
# customize_omtf_dumper will extend the schedule with NanoAOD steps.
process.L1simulation_step = cms.Path(process.SimL1Emulator)
process.endjob_step = cms.EndPath(process.endOfProcess)
process.schedule = cms.Schedule(process.L1simulation_step, process.endjob_step)

# --- Apply OMTF ROOT + NanoAOD dumper ---
from customize_omtf_dumper import customise_omtf_dumper
process = customise_omtf_dumper(process)

# Override NanoAOD output filename
_nano_out = os.environ.get('DAS_NANO_OUTPUT', 'omtf_nano_das.root')
if hasattr(process, 'NANOOMTFoutput'):
    process.NANOOMTFoutput.fileName = cms.untracked.string(_nano_out)
