"""
S5 - Two Displaced Muons (same event), flat in 1/pT, flat in d0.

Uses FlatRandomPtGunProducer2 (EMTFTools/ParticleGuns, already compiled)
with PartID=[-13, 13]: two independent particles per event, each with its
own independently sampled pT, phi, eta, and d0.

Key properties:
  pT: flat in 1/pT over [2, 200] GeV for each muon -> unbiased, no reweighting.
  d0: flat uniform in [0, 30] cm for each muon    -> OC stress-test zone.
  Charges: both randomly flipped independently per event.
  Independence: unlike the LLP-gun approach (D1 topology), these two muons
    are NOT kinematically correlated -- each is independently generated.
    This is actually better for OC training: the network cannot use
    kinematic correlation as a cue to separate overlapping candidates.

Difference from S2: two muons per event (OC repulsion loss required).
Difference from D1: d0 is directly controlled and pT is flat (no H kinematics).

Note: the Delta_phi between the two muons is NOT controlled -- it is random.
Close-Delta_phi events where both muons land in the same OMTF processor window
(~60 deg) occur with ~1/6 probability. Apply sector-window filter at ROOT->PyG
conversion time (see Section 13.5).

eta range: full OMTF barrel acceptance +-[0.82, 1.24]
PU: 0
Target events: 150,000 (post-filter ~25,000 same-window events)
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID          = cms.vint32(-13, 13),       # mu- and mu+; charges independently randomised
        MinPt           = cms.double(2.0),
        MaxPt           = cms.double(200.0),
        MinDxy          = cms.double(0.0),           # [cm] d0 range -- tighter than S2 for OC focus
        MaxDxy          = cms.double(30.0),
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
    psethack        = cms.string('two displaced muons flatOneOverPt flatD0 d0 0-30cm'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
