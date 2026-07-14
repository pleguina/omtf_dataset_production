# Auto generated configuration file
# using: 
# Revision: 1.19 
# Source: /local/reps/CMSSW/CMSSW/Configuration/Applications/python/ConfigBuilder.py,v 
# with command line options: Configuration/GenProduction/python/OMTF_HECIN/C10_disp_pt20to50_overlap.py --python_filename /afs/cern.ch/user/p/pleguina/omtf_dataset_production/configs/C10_disp_pt20to50_overlap_cfg.py --fileout file:C10_disp_pt20to50_overlap.root --eventcontent FEVTSIM --datatier GEN-SIM-DIGI-RAW --conditions 140X_mcRun4_realistic_v4 --geometry Extended2026D110 --era Phase2C17I13M9 --beamspot HLLHC14TeV --step GEN,SIM,DIGI:pdigi_valid,L1 --nThreads 4 --customise_commands "process.trackerGeometry.applyAlignment=cms.bool(False)" --no_exec -n 100
import FWCore.ParameterSet.Config as cms

from Configuration.Eras.Era_Phase2C17I13M9_cff import Phase2C17I13M9

process = cms.Process('L1',Phase2C17I13M9)

# import of standard configurations
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

process.maxEvents = cms.untracked.PSet(
    input = cms.untracked.int32(100),
    output = cms.optional.untracked.allowed(cms.int32,cms.PSet)
)

# Input source
process.source = cms.Source("EmptySource")

process.options = cms.untracked.PSet(
    IgnoreCompletely = cms.untracked.vstring(),
    Rethrow = cms.untracked.vstring(),
    TryToContinue = cms.untracked.vstring(),
    accelerators = cms.untracked.vstring('*'),
    allowUnscheduled = cms.obsolete.untracked.bool,
    canDeleteEarly = cms.untracked.vstring(),
    deleteNonConsumedUnscheduledModules = cms.untracked.bool(True),
    dumpOptions = cms.untracked.bool(False),
    emptyRunLumiMode = cms.obsolete.untracked.string,
    eventSetup = cms.untracked.PSet(
        forceNumberOfConcurrentIOVs = cms.untracked.PSet(
            allowAnyLabel_=cms.required.untracked.uint32
        ),
        numberOfConcurrentIOVs = cms.untracked.uint32(0)
    ),
    fileMode = cms.untracked.string('FULLMERGE'),
    forceEventSetupCacheClearOnNewRun = cms.untracked.bool(False),
    holdsReferencesToDeleteEarly = cms.untracked.VPSet(),
    makeTriggerResults = cms.obsolete.untracked.bool,
    modulesToCallForTryToContinue = cms.untracked.vstring(),
    modulesToIgnoreForDeleteEarly = cms.untracked.vstring(),
    numberOfConcurrentLuminosityBlocks = cms.untracked.uint32(0),
    numberOfConcurrentRuns = cms.untracked.uint32(1),
    numberOfStreams = cms.untracked.uint32(0),
    numberOfThreads = cms.untracked.uint32(1),
    printDependencies = cms.untracked.bool(False),
    sizeOfStackForThreadsInKB = cms.optional.untracked.uint32,
    throwIfIllegalParameter = cms.untracked.bool(True),
    wantSummary = cms.untracked.bool(False)
)

# Production Info
process.configurationMetadata = cms.untracked.PSet(
    annotation = cms.untracked.string('Configuration/GenProduction/python/OMTF_HECIN/C10_disp_pt20to50_overlap.py nevts:100'),
    name = cms.untracked.string('Applications'),
    version = cms.untracked.string('$Revision: 1.19 $')
)

# Output definition

process.FEVTSIMoutput = cms.OutputModule("PoolOutputModule",
    SelectEvents = cms.untracked.PSet(
        SelectEvents = cms.vstring('generation_step')
    ),
    dataset = cms.untracked.PSet(
        dataTier = cms.untracked.string('GEN-SIM-DIGI-RAW'),
        filterName = cms.untracked.string('')
    ),
    fileName = cms.untracked.string('file:C10_disp_pt20to50_overlap.root'),
    outputCommands = process.FEVTSIMEventContent.outputCommands,
    splitLevel = cms.untracked.int32(0)
)

# Additional output definition

# Other statements
process.genstepfilter.triggerConditions=cms.vstring("generation_step")
process.mix.digitizers = cms.PSet(process.theDigitizersValid)
from Configuration.AlCa.GlobalTag import GlobalTag
process.GlobalTag = GlobalTag(process.GlobalTag, '140X_mcRun4_realistic_v4', '')

process.etaFilter = cms.EDFilter("MCSingleParticleFilter",
    MaxEta = cms.untracked.vdouble(1.24, 1.24, -0.82, -0.82),
    MinEta = cms.untracked.vdouble(0.82, 0.82, -1.24, -1.24),
    MinPt = cms.untracked.vdouble(0.0, 0.0, 0.0, 0.0),
    ParticleID = cms.untracked.vint32(13, -13, 13, -13),
    Status = cms.untracked.vint32(1, 1, 1, 1)
)


process.generator = cms.EDProducer("FlatRandomPtGunProducer2",
    AddAntiParticle = cms.bool(False),
    PGunParameters = cms.PSet(
        MaxDxy = cms.double(50.0),
        MaxEta = cms.double(1.24),
        MaxLxy = cms.double(200.0),
        MaxPhi = cms.double(3.14159265359),
        MaxPt = cms.double(50),
        MinDxy = cms.double(0.2),
        MinEta = cms.double(-1.24),
        MinPhi = cms.double(-3.14159265359),
        MinPt = cms.double(20),
        PartID = cms.vint32(-13),
        PtSpectrum = cms.string('flatOneOverPt'),
        RandomCharge = cms.bool(True),
        VertexSpectrum = cms.string('flatD0')
    ),
    Verbosity = cms.untracked.int32(0),
    firstRun = cms.untracked.uint32(1),
    psethack = cms.string('single displaced muon flatOneOverPt 20-50 GeV d0 0.2-50cm OMTF overlap')
)


process.ProductionFilterSequence = cms.Sequence(process.generator+process.etaFilter)

# Path and EndPath definitions
process.generation_step = cms.Path(process.pgen)
process.simulation_step = cms.Path(process.psim)
process.digitisation_step = cms.Path(process.pdigi_valid)
process.L1simulation_step = cms.Path(process.SimL1Emulator)
process.genfiltersummary_step = cms.EndPath(process.genFilterSummary)
process.endjob_step = cms.EndPath(process.endOfProcess)
process.FEVTSIMoutput_step = cms.EndPath(process.FEVTSIMoutput)

# Schedule definition
process.schedule = cms.Schedule(process.generation_step,process.genfiltersummary_step,process.simulation_step,process.digitisation_step,process.L1simulation_step,process.endjob_step,process.FEVTSIMoutput_step)
from PhysicsTools.PatAlgos.tools.helpers import associatePatAlgosToolsTask
associatePatAlgosToolsTask(process)

#Setup FWK for multithreaded
process.options.numberOfThreads = 4
process.options.numberOfStreams = 0
# filter all path with the production filter sequence
for path in process.paths:
	getattr(process,path).insert(0, process.ProductionFilterSequence)



# Customisation from command line

"process.trackerGeometry.applyAlignment=cms.bool(False)"
# Add early deletion of temporary data products to reduce peak memory need
from Configuration.StandardSequences.earlyDeleteSettings_cff import customiseEarlyDelete
process = customiseEarlyDelete(process)
# End adding early deletion

# ===== OMTF DataROOTDumper2 =====
from customize_omtf_dumper import customise_omtf_dumper
process = customise_omtf_dumper(process)

