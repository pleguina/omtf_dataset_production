"""
G18_neg_promptTransitionLow_PU200 — Single Prompt Muon, low-eta barrel-overlap
transition/guard-band region, PU200.

Covers the barrel-overlap boundary (below the OMTF overlap acceptance floor
at |eta|=0.82), complementing G13/G14 (which cover the overlap-endcap
boundary at 1.2-1.6).

Key properties:
  n muons/event: 1  (charge randomised)
  pT: flat in 1/pT over [2, 200] GeV
  eta: -0.90 < eta < -0.70  (low-eta transition/guard band)
  phi: full 2pi
  displacement: prompt (d0 = 0)
  PU: 200

Purpose:
  PU200 hard-negative guard-band at the barrel-overlap boundary. Tests that
  the displaced model does not fire spuriously near the lower acceptance
  boundary in realistic occupancy.
  Part of WP3/WP5 (new_datset_reqs.md Sec 5.4).
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID         = cms.vint32(-13),
        MinPt          = cms.double(2.0),
        MaxPt          = cms.double(200.0),
        MinDxy         = cms.double(0.0),
        MaxDxy         = cms.double(0.0),
        MinEta         = cms.double(-0.90),
        MaxEta         = cms.double(-0.70),
        MinPhi         = cms.double(-3.14159265359),
        MaxPhi         = cms.double( 3.14159265359),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('none'),
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single prompt muon flatOneOverPt 2-200 GeV -0.90<eta<-0.70 transition guard PU200'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
