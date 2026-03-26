"""
S2 - Single Displaced Muon, flat in 1/pT, flat in d0.

Uses FlatRandomPtGunProducer2 (EMTFTools/ParticleGuns, already compiled).

Key properties:
  pT: flat in 1/pT over [2, 200] GeV  -> unbiased curvature coverage, no reweighting needed.
  d0: flat uniform in [0, 50] cm      -> full OMTF displaced coverage.
  Charge: randomised per event (RandomCharge=True).
  Lxy: up to 200 cm (keeps production vertex inside MB1 at r≈231 cm;
       muons born beyond MB1 produce no OMTF hits and are unusable).

This replaces the previous LLP-gun-based approach for this sample:
  - d0 is directly controlled (no indirect ctau -> Lxy -> d0 chain)
  - pT IS flat in 1/pT -> no training-time reweighting required
  - one muon per event (clean single-track topology)

eta range: full OMTF barrel acceptance +-[0.82, 1.24]
PU: 0
Target events: 500,000
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID          = cms.vint32(-13),           # mu-; charge randomised below
        MinPt           = cms.double(2.0),            # [GeV] pT range
        MaxPt           = cms.double(200.0),
        MinDxy          = cms.double(0.0),            # [cm] d0 range, flat uniform
        MaxDxy          = cms.double(50.0),           # [cm] covers full OMTF displaced window
        MaxLxy          = cms.double(200.0),          # [cm] keep vertex inside MB1 (r<231 cm)
        MinEta          = cms.double(-1.24),          # full OMTF acceptance
        MaxEta          = cms.double( 1.24),
        MinPhi          = cms.double(-3.14159265359),
        MaxPhi          = cms.double( 3.14159265359),
        PtSpectrum      = cms.string('flatOneOverPt'),
        VertexSpectrum  = cms.string('flatD0'),
        RandomCharge    = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single displaced muon flatOneOverPt flatD0 d0 0-50cm'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
