# test_omtf_nano_cfg.py
# Minimal test cfg: reads an existing EDM POOL file (B1_0.root)
# and produces the OMTF NanoAOD table + GenMuon table.
#
# Usage (after scram b):
#   cmsRun test_omtf_nano_cfg.py
#
# Expected output: test_omtf_nano.root with Events/omtf_* and Events/GenMuon_*

import FWCore.ParameterSet.Config as cms
from Configuration.Eras.Era_Phase2C17I13M9_cff import Phase2C17I13M9

process = cms.Process('NANO', Phase2C17I13M9)

process.load('FWCore.MessageService.MessageLogger_cfi')
process.MessageLogger.cerr.FwkReport.reportEvery = 10

process.maxEvents = cms.untracked.PSet(
    input = cms.untracked.int32(100)
)

# Input: the local EDM POOL file produced by a previous GEN+SIM+DIGI+L1 run
process.source = cms.Source("PoolSource",
    fileNames = cms.untracked.vstring(
        'file:/afs/cern.ch/user/p/pleguina/omtf_hecin_dataset_production/B1_0.root'
    ),
)

# ---- OMTF NanoAOD table producers ----
from L1Trigger.L1MuNano.omtfNanoTables_cff import (
    OMTFTrackTable,
    genMuonNanoTable,
    p2OmtfNanoTablesTask,
)

process.OMTFTrackTable   = OMTFTrackTable
process.genMuonNanoTable = genMuonNanoTable

process.p2OmtfNanoTablesTask = cms.Task(
    process.OMTFTrackTable,
    process.genMuonNanoTable,
)
process.nanoTablesStep = cms.Path(process.p2OmtfNanoTablesTask)

# ---- NanoAOD output module ----
process.NANOOMTFoutput = cms.OutputModule(
    "NanoAODOutputModule",
    compressionAlgorithm = cms.untracked.string('LZMA'),
    compressionLevel     = cms.untracked.int32(9),
    saveProvenance       = cms.untracked.bool(True),
    fileName             = cms.untracked.string('test_omtf_nano.root'),
    outputCommands = cms.untracked.vstring(
        'drop *',
        'keep nanoaodFlatTable_*Table_*_*',
    ),
)
process.NANOOMTFoutput_step = cms.EndPath(process.NANOOMTFoutput)

process.schedule = cms.Schedule(
    process.nanoTablesStep,
    process.NANOOMTFoutput_step,
)
