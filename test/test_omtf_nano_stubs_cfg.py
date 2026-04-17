# test_omtf_nano_stubs_cfg.py
#
# Validates that the MuonStubTpsTable and MuonStubKmtfTable producers populate
# the NanoAOD output with hybrid stubs (coord1/coord2/eta1/eta2).
#
# Runs 2 events of GEN+SIM+DIGI+L1 from a single-muon gun in the OMTF eta range,
# no pileup, the same era/geometry as the HECIN production jobs.
# l1tStubsGmt runs as part of SimL1Emulator (phase2_trigger active in Phase2C17I13M9).
#
# Usage:
#   cd omtf_hecin_dataset_production
#   cmsRun test/test_omtf_nano_stubs_cfg.py
#   python3 -c "import uproot; t=uproot.open('test/test_omtf_nano_stubs.root:Events'); print([k for k in t.keys() if 'Stub' in k])"

import FWCore.ParameterSet.Config as cms
from Configuration.Eras.Era_Phase2C17I13M9_cff import Phase2C17I13M9

process = cms.Process('L1', Phase2C17I13M9)

process.load('Configuration.StandardSequences.Services_cff')
process.load('SimGeneral.HepPDTESSource.pythiapdt_cfi')
process.load('FWCore.MessageService.MessageLogger_cfi')
process.load('Configuration.EventContent.EventContent_cff')
process.load('SimGeneral.MixingModule.mixNoPU_cfi')
process.load('Configuration.Geometry.GeometryExtended2026D110Reco_cff')
process.load('Configuration.Geometry.GeometryExtended2026D110_cff')
process.load('Configuration.StandardSequences.MagneticField_cff')
process.load('Configuration.StandardSequences.Generator_cff')
process.load('IOMC.EventVertexGenerators.VtxSmearedHLLHC14TeV_cfi')
process.load('GeneratorInterface.Core.genFilterSummary_cff')
process.load('Configuration.StandardSequences.SimIdeal_cff')
process.load('Configuration.StandardSequences.Digi_cff')
process.load('Configuration.StandardSequences.SimL1Emulator_cff')
process.load('Configuration.StandardSequences.EndOfProcess_cff')
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')

process.maxEvents = cms.untracked.PSet(input=cms.untracked.int32(2))

process.source = cms.Source("EmptySource")

process.options = cms.untracked.PSet(
    numberOfThreads = cms.untracked.uint32(1),
    numberOfStreams = cms.untracked.uint32(0),
    wantSummary     = cms.untracked.bool(True),
)

from Configuration.AlCa.GlobalTag import GlobalTag
process.GlobalTag = GlobalTag(process.GlobalTag, '140X_mcRun4_realistic_v4', '')

# Single muon gun in OMTF eta range
process.generator = cms.EDProducer("FlatRandomOneOverPtGunProducer",
    AddAntiParticle = cms.bool(False),
    PGunParameters = cms.PSet(
        MaxEta       = cms.double(1.24),
        MaxOneOverPt = cms.double(0.5),
        MaxPhi       = cms.double(3.14159265359),
        MinEta       = cms.double(-1.24),
        MinOneOverPt = cms.double(0.005),
        MinPhi       = cms.double(-3.14159265359),
        PartID       = cms.vint32(-13, 13),
    ),
    Verbosity = cms.untracked.int32(0),
    firstRun  = cms.untracked.uint32(1),
    psethack  = cms.string('single muon flat 1/pT 2-200'),
)

process.mix.digitizers = cms.PSet(process.theDigitizersValid)

# Disable tracker alignment (geometry mismatch in CMSSW_14_2_0_pre2)
if hasattr(process, 'trackerGeometry'):
    process.trackerGeometry.applyAlignment = cms.bool(False)

process.ProductionFilterSequence = cms.Sequence(process.generator)

process.generation_step    = cms.Path(process.pgen)
process.simulation_step    = cms.Path(process.psim)
process.digitisation_step  = cms.Path(process.pdigi_valid)
process.L1simulation_step  = cms.Path(process.SimL1Emulator)
process.genfiltersummary_step = cms.EndPath(process.genFilterSummary)
process.endjob_step        = cms.EndPath(process.endOfProcess)

process.schedule = cms.Schedule(
    process.generation_step,
    process.genfiltersummary_step,
    process.simulation_step,
    process.digitisation_step,
    process.L1simulation_step,
    process.endjob_step,
)

for path in process.paths:
    getattr(process, path).insert(0, process.ProductionFilterSequence)

# Apply NanoAOD customisation (adds stub tables via l1tStubsGmt)
import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + '/configs')
from customize_omtf_dumper import customise_omtf_nano
process = customise_omtf_nano(process, nano_filename="test/test_omtf_nano_stubs.root")
