"""
customize_omtf_dumper.py — CMSSW customisation function to add
the OMTF Phase-2 DataROOTDumper2 (produces simOmtfPhase2Digis/OMTFHitsTree).

This must be placed in the configs/ directory alongside the cmsDriver configs.
It is imported at the end of each _cfg.py file.
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

    return process
