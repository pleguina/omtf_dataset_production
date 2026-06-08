"""
C18_prompt_pt20to50_overlap_PU200 — Single Prompt Muon, OMTF overlap region, PU200.
pT-binned controlled sample for displaced recovery validation at realistic occupancy.

Key properties:
  n muons/event: 1  (charge randomised)
  pT: flat in 1/pT over [20, 50] GeV  (controlled pT bin)
  eta: 0.82 < |eta| < 1.24  (OMTF overlap acceptance)
  phi: full 2pi
  displacement: prompt-like (|d0| < 0.05 cm)
  PU: 200

Purpose:
  PU200 prompt control for displaced discrimination at realistic occupancy.
  Part of WP4/WP5/WP6 (new_datset_reqs.md).
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID         = cms.vint32(-13),
        MinPt          = cms.double(20),
        MaxPt          = cms.double(50),
        MinDxy         = cms.double(0.0),
        MaxDxy         = cms.double(0.05),
        MaxLxy         = cms.double(10.0),
        MinEta         = cms.double(-1.24),
        MaxEta         = cms.double( 1.24),
        MinPhi         = cms.double(-3.14159265359),
        MaxPhi         = cms.double( 3.14159265359),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('flatD0'),
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single prompt muon flatOneOverPt 20-50 GeV d0<0.05cm OMTF overlap PU200'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

etaFilter = cms.EDFilter("MCParticleModuloFilter",
    moduleLabel = cms.InputTag("generator", "unsmeared"),
    minEta      = cms.double(0.82),
    maxEta      = cms.double(1.24),
    absetaMode  = cms.bool(True),
    status      = cms.int32(1),
    particleID  = cms.vint32(13, -13),
)

ProductionFilterSequence = cms.Sequence(generator * etaFilter)
