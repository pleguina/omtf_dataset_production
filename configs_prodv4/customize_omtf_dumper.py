"""
customize_omtf_dumper.py — CMSSW customisation function to add
the OMTF Phase-2 DataROOTDumper2 (produces simOmtfPhase2Digis/OMTFHitsTree)
and a NanoAOD-style output (OMTF track table + generator muon table).

This must be placed in the configs/ directory alongside the cmsDriver configs.
It is imported at the end of each _cfg.py file.

NanoAOD output requires the locally compiled package L1Trigger/L1MuNano and
DPGAnalysis/Phase2L1TNanoAOD (SimpleTriggerL1SAMuonFlatTableProducer plugin).
"""
import os

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
        # Also dump the candidates killed by the ghost buster (killed=1, omtfQuality=0)
        process.simOmtfPhase2Digis.dumpKilledOmtfCands = cms.bool(True)

        # Enable candidate-to-simtrack matching for truth labels in OMTFHitsTree.
        # Use simpleMatching: matches on gen-level eta/phi proximity without propagation,
        # so it doesn't need the SteppingHelixPropagator (not in GEN,SIM,DIGI,L1 workflow).
        process.simOmtfPhase2Digis.candidateSimMuonMatcher = cms.bool(True)
        process.simOmtfPhase2Digis.candidateSimMuonMatcherType = cms.string("simpleMatching")
        process.simOmtfPhase2Digis.muonMatcherFile = cms.FileInPath(
            "L1Trigger/L1TMuon/data/omtf_config/muonMatcherHists_100files_smoothStdDev_withOvf.root"
        )

        # Frozen production propagation mode (propagation smoke audit, item 3). MUST stay in
        # lockstep with candidateSimMuonMatcherType above -- the producer logs a one-time
        # LogWarning if they drift apart. "disabled_simpleMatching_v1" is the only mode currently
        # exercised by this repo: TpMuon_prop*St1/St2 all report "not implemented"/"disabled"
        # (propOkStX=0, coordinates=NaN) rather than a station-2-proxied value. Do NOT switch to
        # "steppingHelix_surfaceAware_v1" (which requires candidateSimMuonMatcherType=
        # "withPropagator"/"simplePropagation" + the SteppingHelixPropagator ESProducer loaded)
        # for a full-dataset relaunch without first running the pilot checklist in
        # src/MANIFEST.md (no segfault; EventSetup records available; nonzero propagation
        # success; finite coordinates; independent St1/St2 results; valid surface codes;
        # hard-scatter GenMuon/TpMuon propagation closure).
        process.simOmtfPhase2Digis.propagationMode = cms.string("disabled_simpleMatching_v1")

        # Point to sim-truth collections
        process.simOmtfPhase2Digis.simTracksTag = cms.InputTag("g4SimHits")
        process.simOmtfPhase2Digis.simVertexesTag = cms.InputTag("g4SimHits")
        process.simOmtfPhase2Digis.trackingParticleTag = cms.InputTag("mix", "MergedTrackTruth")

        # Digi-sim link collections for per-stub SimTrack truth labeling in
        # OMTFAllInputTree (reg_stub_trackId / reg_stub_ambiguous branches).
        # Omit these lines to disable truth labeling (branches will be zero-filled).
        process.simOmtfPhase2Digis.dtDigiSimLinksInputTag       = cms.InputTag("simMuonDTDigis", "")
        process.simOmtfPhase2Digis.rpcDigiSimLinkInputTag        = cms.InputTag("simMuonRPCDigis", "RPCDigiSimLink")
        process.simOmtfPhase2Digis.cscStripDigiSimLinksInputTag  = cms.InputTag("simMuonCSCDigis", "MuonCSCStripDigiSimLinks")
        process.simOmtfPhase2Digis.genParticleTag                = cms.InputTag("genParticles")

        # Pileup-provenance source (reg_pu_src_* / TpMuon_puSourceIdx resolution).
        # Standard PileupSummaryInfo product from the Mixing module (present whenever
        # PU is mixed in, i.e. any non-empty process.mix.input.fileNames). Omitting this
        # tag silently disables the whole reg_pu_src_*/puSourceIdx block (it stays -1/empty).
        process.simOmtfPhase2Digis.pileupInfoInputTag = cms.InputTag("addPileupInfo")

        # Schema-metadata producer-commit provenance (OMTFSchemaMetadata.producerGitCommit).
        # Populated from OMTF_PRODUCER_GIT_COMMIT, which the condor launch scripts
        # (run_job.sh / run_job_prodv2.sh / run_local_test.sh / run_das_job.sh) must export
        # as `git rev-parse HEAD` before invoking cmsRun. Falls back to the producer's own
        # documented placeholder if unset (e.g. ad-hoc interactive runs).
        process.simOmtfPhase2Digis.producerGitCommit = cms.string(
            os.environ.get("OMTF_PRODUCER_GIT_COMMIT", "UNSET_fill_from_production_manifest")
        )

        # prodv4 schema metadata: dataset identity (OMTFSchemaMetadata.geometry/era/globalTag/datasetName/campaignId).
        # geometry, era, dataset and campaign come from OMTF_GEOMETRY / OMTF_ERA / OMTF_DATASET / OMTF_CAMPAIGN_ID
        # (set by the job script); the global tag is read from the process.
        if hasattr(process, 'GlobalTag') and hasattr(process.GlobalTag, 'globaltag'):
            process.simOmtfPhase2Digis.globalTag = cms.string(str(process.GlobalTag.globaltag.value()))

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

    # ----- 3b. Optional CSC CCLUT on the TMBs (prodv3 variant, NOT the official config) -----
    # The Phase-2 era runs the CCLUT only on the OTMBs (ME1/1, ME2/1), so the overlap CSCs (ME1/2,
    # ME1/3, ME2/2, ME3/2) have no LCT slope. OMTF_CSC_CCLUT_TMB=1 turns it on for those chambers
    # too; this changes the LCT position OMTF sees, so such files are not an upstream baseline.
    if os.environ.get("OMTF_CSC_CCLUT_TMB", "0") == "1" and hasattr(process, 'simCscTriggerPrimitiveDigis'):
        process.simCscTriggerPrimitiveDigis.commonParam.runCCLUT_TMB = cms.bool(True)
        print("customise_omtf_dumper: CSC CCLUT enabled on the TMBs (OMTF_CSC_CCLUT_TMB=1)")

    # ----- 3c. Optional OMTF BX window (multi-BX timing variant, e.g. OMTF_BX_RANGE=-1,1) -----
    # The default is BX 0 only, as upstream. The GMT stub association stays at BX 0.
    bx_range = os.environ.get("OMTF_BX_RANGE", "")
    if bx_range and hasattr(process, 'simOmtfPhase2Digis'):
        bx_min, bx_max = (int(x) for x in bx_range.split(","))
        process.simOmtfPhase2Digis.bxMin = cms.int32(bx_min)
        process.simOmtfPhase2Digis.bxMax = cms.int32(bx_max)
        print(f"customise_omtf_dumper: OMTF BX window [{bx_min}, {bx_max}] (OMTF_BX_RANGE)")

    # ----- 3d. Second OMTF instance with the NN pT assignment (extra baseline, not upstream default) -----
    # Upstream does not set neuralNetworkFile, so the official constrained pT is the pattern pT. This clone
    # runs the same emulator with the NN (lutNN v435, the only weights file available) and no dumpers;
    # it runs unscheduled, only for its NanoAOD tables omtfCNN / omtfUNN. OMTF_NN_BASELINE=0 disables it.
    if os.environ.get("OMTF_NN_BASELINE", "1") == "1" and hasattr(process, 'simOmtfPhase2Digis'):
        nn = process.simOmtfPhase2Digis.clone(
            neuralNetworkFile = cms.FileInPath("L1Trigger/L1TMuon/data/omtf_config/lutNN_omtfRegression_v435_FP.xml"),
            dumpHitsToROOT = cms.bool(False),
            candidateSimMuonMatcher = cms.bool(False),
        )
        for flag in ("dumpResultToXML", "dumpDetailedResultToXML", "dumpGPToXML", "dumpDigisToCSV",
                     "dumpDetailedDebug", "eventCaptureDebug", "dumpKilledOmtfCands"):
            if hasattr(nn, flag):
                setattr(nn, flag, cms.bool(False))
        process.simOmtfPhase2DigisNN = nn

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
        └── Events/omtfC_*, omtfU_* — OMTF constrained / unconstrained SAMuons
        └── Events/gmtFwd*_*       — GMT forward translator (OMTF+EMTF) prompt / displaced
        └── Events/gmtSA*_*        — GMT standalone prompt / displaced (menu input)
        └── Events/kmtfDisp_*      — KMTF displaced SAMuons
        └── Events/GenMuon_*       — generator-level stable muons
        └── Events/DTPhiDigi_*     — Run-3 DT phi primitives (phi, phiB)
        └── Events/Ph2DTPhiDigi_*  — Phase-2 DT phi primitives (phi, phiBend, t0, chi2)
        └── Events/Ph2DTThDigi_*   — Phase-2 DT theta primitives (z, k, t0, chi2)
        └── Events/CSCLctDigi_*    — CSC correlated LCTs (slope, bend, keywire, strip)
        └── Events/RPCDigi_*       — RPC strip digis (strip, bx)

    Requires the locally compiled plugin:
      SimpleTriggerL1SAMuonFlatTableProducer (DPGAnalysis/Phase2L1TNanoAOD)
      DTPhiDigiFlatTableProducer, Ph2DTPhiDigiFlatTableProducer,
      Ph2DTThetaDigiFlatTableProducer, CSCLCTDigiFlatTableProducer,
      RPCDigiFlatTableProducer  (all in L1Trigger/L1MuNano)

    SimpleGenParticleFlatTableProducer is in the base PhysicsTools/NanoAOD package.
    """
    from L1Trigger.L1MuNano.omtfNanoTables_cff import (
        saMuonTables,
        genMuonNanoTable,
        genParticlePropagator,
        genMuonProvenance,
        MuonStubTpsTable,
        MuonStubKmtfTable,
        DTPhiDigiTable,
        Ph2DTPhiDigiTable,
        Ph2DTThetaDigiTable,
        CSCLCTDigiTable,
        RPCDigiTable,
        PileupTable,
        p2OmtfNanoTablesTask,
    )

    # SteppingHelixPropagator ESProducers required by GenParticlePropagator.
    # PropagateToMuonSetup uses the named Along/Any/Opposite variants, not the
    # generic SteppingHelixPropagator_cfi (which only provides ComponentName='SteppingHelixPropagator').
    process.load("TrackPropagation.SteppingHelixPropagator.SteppingHelixPropagatorAlong_cfi")
    process.load("TrackPropagation.SteppingHelixPropagator.SteppingHelixPropagatorAny_cfi")
    process.load("TrackPropagation.SteppingHelixPropagator.SteppingHelixPropagatorOpposite_cfi")

    # Register table producers on the process
    process.genParticlePropagator  = genParticlePropagator
    process.genMuonProvenance      = genMuonProvenance
    if hasattr(process, 'simOmtfPhase2DigisNN'):
        from L1Trigger.L1MuNano.omtfNanoTables_cff import _saMuTable
        saMuonTables = saMuonTables + [
            _saMuTable(("simOmtfPhase2DigisNN", "constrCands"), "omtfCNN",
                       "OMTF constrained SAMuons with the NN pT (lutNN v435), not the upstream default"),
            _saMuTable(("simOmtfPhase2DigisNN", "unconstrCands"), "omtfUNN",
                       "OMTF unconstrained SAMuons from the NN instance"),
        ]
    for t in saMuonTables:
        setattr(process, t.name.value() + "Table", t)  # omtfCTable, omtfUTable, gmtSATable, ...
    process.genMuonNanoTable       = genMuonNanoTable
    process.MuonStubTpsTable       = MuonStubTpsTable
    process.MuonStubKmtfTable      = MuonStubKmtfTable
    process.DTPhiDigiTable         = DTPhiDigiTable
    process.Ph2DTPhiDigiTable      = Ph2DTPhiDigiTable
    process.Ph2DTThetaDigiTable    = Ph2DTThetaDigiTable
    process.CSCLCTDigiTable        = CSCLCTDigiTable
    process.RPCDigiTable           = RPCDigiTable
    process.PileupTable            = PileupTable

    # Dedicated Path so the producers are scheduled before the output EndPath.
    # Using a cms.Task inside a cms.Path is the standard NanoAOD pattern.
    process.p2OmtfNanoTablesTask = cms.Task(
        process.genParticlePropagator,
        process.genMuonProvenance,
        *[getattr(process, t.name.value() + "Table") for t in saMuonTables],
        *([process.simOmtfPhase2DigisNN] if hasattr(process, 'simOmtfPhase2DigisNN') else []),
        process.genMuonNanoTable,
        process.MuonStubTpsTable,
        process.MuonStubKmtfTable,
        process.DTPhiDigiTable,
        process.Ph2DTPhiDigiTable,
        process.Ph2DTThetaDigiTable,
        process.CSCLCTDigiTable,
        process.RPCDigiTable,
        process.PileupTable,
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
    # For GEN-level configs (G1-G4) that have an etaFilter in generation_step,
    # add SelectEvents to prevent the EndPath from running for events that failed the
    # generation-level filter. In CMSSW 14.x, SelectEvents is checked before prefetch
    # for output modules, so this avoids a ProductNotFound cascade via on-demand Tasks.
    if hasattr(process, 'generation_step'):
        process.NANOOMTFoutput.SelectEvents = cms.untracked.PSet(
            SelectEvents = cms.vstring('generation_step')
        )

    process.NANOOMTFoutput_step = cms.EndPath(process.NANOOMTFoutput)

    # Extend the schedule: producers first, then the output EndPath.
    process.schedule.extend([process.nanoTablesStep, process.NANOOMTFoutput_step])

    return process
