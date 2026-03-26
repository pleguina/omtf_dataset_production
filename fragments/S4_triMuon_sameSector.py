"""
S4 - TriMuon Prompt, same OMTF processor window.

Uses FlatRandomPtGunProducer2 (EMTFTools/ParticleGuns, already compiled).

Strategy to enforce same-window: identical to S3.
  All three muons constrained to phi in [-pi/6, +pi/6] and eta in
  [0.82, 1.24]. All three are in OMTF processor sector 0 by construction.
  100% same-window efficiency, no post-filter.

Delta_phi distribution (each pair):
  Follows a triangular distribution on [0, pi/3]. The minimum pair
  |delta_phi| in a 3-muon event is further biased toward small values,
  creating frequent overlapping-stub scenarios -- the hardest ghost buster
  stress-test.

pT: flat in 1/pT over [5, 80] GeV per muon.
Charge: independently randomised per muon (RandomCharge=True).
Vertex: prompt.
eta: positive OMTF endcap only [0.82, 1.24].
PU: 0
Target events: 100,000
"""
import FWCore.ParameterSet.Config as cms
import math

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID          = cms.vint32(-13, 13, -13),  # 3 muons; charges independently randomised
        MinPt           = cms.double(5.0),
        MaxPt           = cms.double(80.0),
        MinDxy          = cms.double(0.0),
        MaxDxy          = cms.double(0.0),
        MinEta          = cms.double(0.82),           # positive OMTF endcap
        MaxEta          = cms.double(1.24),
        MinPhi          = cms.double(-math.pi / 6),  # 60-deg window centred at phi=0
        MaxPhi          = cms.double( math.pi / 6),
        PtSpectrum      = cms.string('flatOneOverPt'),
        VertexSpectrum  = cms.string('none'),
        RandomCharge    = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('trimuon prompt same phi-sector flatOneOverPt 5-80 GeV'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
