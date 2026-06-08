"""
G13_neg_promptTransition — Single Prompt Muon, transition/guard-band region, no PU.

Covers the barrel-endcap transition region between the OMTF overlap acceptance
and the full endcap, complementing G9/G10 (which cover 1.3–1.8).

Key properties:
  n muons/event: 1  (charge randomised)
  pT: flat in 1/pT over [2, 200] GeV
  eta: -1.6 < eta < -1.2  (transition/guard band)
  phi: full 2pi
  displacement: prompt (d0 = 0)
  PU: none

Purpose:
  Hard-negative guard-band sample.  Muons near the overlap boundary that
  should NOT produce an overlap candidate.
  Part of WP3/WP4 (new_datset_reqs.md Sec 5.1 / Sec 5.4).
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID         = cms.vint32(-13),
        MinPt          = cms.double(2.0),
        MaxPt          = cms.double(200.0),
        MinDxy         = cms.double(0.0),
        MaxDxy         = cms.double(0.0),
        MinEta         = cms.double(-1.6),
        MaxEta         = cms.double(-1.2),
        MinPhi         = cms.double(-3.14159265359),
        MaxPhi         = cms.double( 3.14159265359),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('none'),
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single prompt muon flatOneOverPt 2-200 GeV -1.6<eta<-1.2 transition guard'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
