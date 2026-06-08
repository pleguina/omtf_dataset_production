"""
G9_pos - Hard negative: single high-eta muon, no PU, positive eta.

Purpose:
  Real endcap-like muon stubs outside OMTF overlap acceptance,
  with overlap target multiplicity = 0.

Key properties:
  n muons/event: 1
  pT: flat in 1/pT over [2, 200] GeV
  eta: 1.30 < eta < 1.80
  phi: full 2pi
  d0: 0 (prompt)
  PU: none
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID         = cms.vint32(-13),
        MinPt          = cms.double(2.0),
        MaxPt          = cms.double(200.0),
        MinDxy         = cms.double(0.0),
        MaxDxy         = cms.double(0.0),
        MinEta         = cms.double(1.30),
        MaxEta         = cms.double(1.80),
        MinPhi         = cms.double(-3.14159265359),
        MaxPhi         = cms.double(3.14159265359),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('none'),
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single high-eta muon flatOneOverPt 2-200 GeV 1.30<eta<1.80 hard-negative'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
