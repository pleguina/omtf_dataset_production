"""
customize_omtf_dumper.py — CMSSW customisation function to add
the OMTF Phase-2 DataROOTDumper2 (produces simOmtfPhase2Digis/OMTFHitsTree)
and a NanoAOD-style output (OMTF track table + generator muon table).

This must be placed in the configs/ directory alongside the cmsDriver configs.
It is imported at the end of each _cfg.py file.

NanoAOD output requires the locally compiled package L1Trigger/L1MuNano
(SimpleOMTFTrackCandidateFlatTableProducer plugin).
"""
import FWCore.ParameterSet.Config as cms


def customise_omtf_dumper(process):
    """
    Activate the OMTF Phase-2 emulator hit dumper.

    Requires the OMTF emulator (simOmtfPhase2Digis) to already be in the process
    schedule from the L1 step.
    """

    # ----- 1. TFileService for ROOT output -----
    if not hasattr(process, 'TFileService'):
        process.TFileService = cms.Service(
            "TFileService",
            fileName=cms.string("omtf_hits.root"),
        )

    # ----- 2. Configure the OMTF emulator for dumper mode -----
    if hasattr(process, 'simOmtfPhase2Digis'):
        # Enable the hit-level ROOT dumper
        process.simOmtfPhase2Digis.dumpHitsToROOT = cms.bool(True)

        # Enable candidate-to-simtrack matching for truth labels in OMTFHitsTree.
        # Use simpleMatching: matches on gen-level eta/phi proximity without propagation,
        # so it doesn't need the SteppingHelixPropagator (not in GEN,SIM,DIGI,L1 workflow).
        process.simOmtfPhase2Digis.candidateSimMuonMatcher = cms.bool(True)
        process.simOmtfPhase2Digis.candidateSimMuonMatcherType = cms.string("simpleMatching")
        process.simOmtfPhase2Digis.muonMatcherFile = cms.FileInPath(
            "L1Trigger/L1TMuon/data/omtf_config/muonMatcherHists_100files_smoothStdDev_withOvf.root"
        )

        # Point to sim-truth collections
        process.simOmtfPhase2Digis.simTracksTag = cms.InputTag("g4SimHits")
        process.simOmtfPhase2Digis.simVertexesTag = cms.InputTag("g4SimHits")

        # Digi-sim link collections for per-stub SimTrack truth labeling in
        # OMTFAllInputTree (reg_stub_trackId / reg_stub_ambiguous branches).
        # Omit these lines to disable truth labeling (branches will be zero-filled).
        process.simOmtfPhase2Digis.dtDigiSimLinksInputTag       = cms.InputTag("simMuonDTDigis", "")
        process.simOmtfPhase2Digis.rpcDigiSimLinkInputTag        = cms.InputTag("simMuonRPCDigis", "RPCDigiSimLink")
        process.simOmtfPhase2Digis.cscStripDigiSimLinksInputTag  = cms.InputTag("simMuonCSCDigis", "MuonCSCStripDigiSimLinks")
        process.simOmtfPhase2Digis.genParticleTag                = cms.InputTag("genParticles")

    else:
        import sys
        print("WARNING: simOmtfPhase2Digis not found in process — OMTF dumper NOT configured.", file=sys.stderr)

    # ----- 3. Slim the FEVTSIM output to save disk -----
    if hasattr(process, 'FEVTSIMoutput'):
        process.FEVTSIMoutput.outputCommands = cms.untracked.vstring(
            'drop *',
            'keep *_simOmtfPhase2Digis_*_*',
            'keep *_genParticles_*_*',
            'keep *_g4SimHits_*_*',
        )

    # ----- 4. Disable tracker alignment (DB payload has 43708 modules, -----
    # ----- geometry has 43600 in CMSSW_14_2_0_pre2 — safe for L1 studies) -----
    if hasattr(process, 'trackerGeometry'):
        process.trackerGeometry.applyAlignment = cms.bool(False)

    # ----- 5. NanoAOD-style output: OMTF track table + generator muon table -----
    process = customise_omtf_nano(process)

    return process


def customise_omtf_nano(process, nano_filename="omtf_nano.root"):
    """
    Add NanoAOD-style FlatTable producers and a NanoAODOutputModule.

    Produces:
      omtf_nano.root  (NanoAOD TTree format)
        └── Events/omtf_*     — OMTF track candidates at BX=0
        └── Events/GenMuon_*  — generator-level stable muons

    Requires the locally compiled plugin:
      SimpleOMTFTrackCandidateFlatTableProducer
      (package L1Trigger/L1MuNano, BXVectorSimpleFlatTableProducer<l1t::RegionalMuonCand>)

    SimpleGenParticleFlatTableProducer is in the base PhysicsTools/NanoAOD package.
    """
    from L1Trigger.L1MuNano.omtfNanoTables_cff import (
        OMTFTrackTable,
        genMuonNanoTable,
        MuonStubTpsTable,
        MuonStubKmtfTable,
        p2OmtfNanoTablesTask,
    )

    # Register table producers on the process
    process.OMTFTrackTable   = OMTFTrackTable
    process.genMuonNanoTable = genMuonNanoTable
    process.MuonStubTpsTable  = MuonStubTpsTable
    process.MuonStubKmtfTable = MuonStubKmtfTable

    # Dedicated Path so the producers are scheduled before the output EndPath.
    # Using a cms.Task inside a cms.Path is the standard NanoAOD pattern.
    process.p2OmtfNanoTablesTask = cms.Task(
        process.OMTFTrackTable,
        process.genMuonNanoTable,
        process.MuonStubTpsTable,
        process.MuonStubKmtfTable,
    )
    process.nanoTablesStep = cms.Path(process.p2OmtfNanoTablesTask)

    # NanoAODOutputModule: writes a self-describing ROOT file with TTrees.
    # outputCommands keeps all FlatTable products (module labels ending in *Table).
    process.NANOOMTFoutput = cms.OutputModule(
        "NanoAODOutputModule",
        compressionAlgorithm = cms.untracked.string('LZMA'),
        compressionLevel     = cms.untracked.int32(9),
        saveProvenance       = cms.untracked.bool(True),
        fileName             = cms.untracked.string(nano_filename),
        outputCommands = cms.untracked.vstring(
            'drop *',
            'keep nanoaodFlatTable_*Table_*_*',  # all FlatTable products
        ),
    )
    process.NANOOMTFoutput_step = cms.EndPath(process.NANOOMTFoutput)

    # Extend the schedule: producers first, then the output EndPath.
    process.schedule.extend([process.nanoTablesStep, process.NANOOMTFoutput_step])

    return process
