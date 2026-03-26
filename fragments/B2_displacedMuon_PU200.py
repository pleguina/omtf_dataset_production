"""
B2 — Displaced Muon + PU200.

Uses FlatRandomPtGunProducer2 (EMTFTools/ParticleGuns, already compiled).

Key properties:
  pT: flat in 1/pT over [2, 200] GeV   → unbiased, no reweighting needed.
  d0: flat uniform in [0, 50] cm        → matches S2 displaced coverage.
  Charge: randomised per event.
  PU: 200 (added via mixer in cfg).

Note: the previous version used FlatRandomOneOverPtGunProducer which does NOT
apply any vertex displacement. That meant B2 was effectively a prompt muon + PU200
with a misleading comment. This version correctly adds flat-d0 displacement.

eta range: full OMTF barrel acceptance ±[0.82, 1.24]
PU: 200
Target events: 200,000
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID          = cms.vint32(-13),           # mu-; charge randomised below
        MinPt           = cms.double(2.0),
        MaxPt           = cms.double(200.0),
        MinDxy          = cms.double(0.0),           # [cm] flat d0 range
        MaxDxy          = cms.double(50.0),
        MaxLxy          = cms.double(200.0),          # [cm] keep vertex inside MB1 (r<231 cm)
        MinEta          = cms.double(-1.24),
        MaxEta          = cms.double( 1.24),
        MinPhi          = cms.double(-3.14159265359),
        MaxPhi          = cms.double( 3.14159265359),
        PtSpectrum      = cms.string('flatOneOverPt'),
        VertexSpectrum  = cms.string('flatD0'),
        RandomCharge    = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('displaced muon flatOneOverPt flatD0 d0 0-50cm PU200'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
