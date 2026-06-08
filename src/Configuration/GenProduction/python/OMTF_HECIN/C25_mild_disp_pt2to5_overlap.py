"""
C25_mild_disp_pt2to5_overlap — Single Mild-Displaced Muon, OMTF overlap region.
Mild displacement (|d0| 0.05–0.2 cm): the critical ambiguity zone between
prompt-like and clearly displaced. Most dangerous fake source for displaced model.

Key properties:
  n muons/event: 1  (charge randomised)
  pT: flat in 1/pT over [2, 5] GeV  (controlled pT bin)
  eta: 0.82 < |eta| < 1.24  (OMTF overlap acceptance)
  phi: full 2pi
  displacement: mild displaced (|d0| 0.05–0.2 cm)
  PU: none

Purpose:
  Critical ambiguity sample: muons that are mildly off-beam but not clearly
  displaced. Tests whether displaced model misidentifies these as signal.
  Part of WP4 prompt-vs-displaced discrimination study (new_datset_reqs.md Sec 5.1).
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID         = cms.vint32(-13),
        MinPt          = cms.double(2),
        MaxPt          = cms.double(5),
        MinDxy         = cms.double(0.05),           # mild displaced lower bound
        MaxDxy         = cms.double(0.2),            # mild displaced upper bound
        MaxLxy         = cms.double(50.0),
        MinEta         = cms.double(-1.24),
        MaxEta         = cms.double( 1.24),
        MinPhi         = cms.double(-3.14159265359),
        MaxPhi         = cms.double( 3.14159265359),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('flatD0'),
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single mild-displaced muon flatOneOverPt 2-5 GeV d0 0.05-0.2cm OMTF overlap'),
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
