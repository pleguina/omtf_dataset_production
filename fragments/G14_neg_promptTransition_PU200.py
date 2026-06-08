"""
G14_neg_promptTransition_PU200 — Single Prompt Muon, transition/guard-band region, PU200.

PU200 version of G13.  Used for rate-proxy validation near the overlap boundary.

Key properties:
  n muons/event: 1  (charge randomised)
  pT: flat in 1/pT over [2, 200] GeV
  eta: -1.6 < eta < -1.2  (transition/guard band)
  phi: full 2pi
  displacement: prompt (d0 = 0)
  PU: 200

Purpose:
  PU200 hard-negative guard-band.  Tests that the displaced model does not
  fire spuriously near the acceptance boundary in realistic occupancy.
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
        MinEta         = cms.double(-1.6),
        MaxEta         = cms.double(-1.2),
        MinPhi         = cms.double(-3.14159265359),
        MaxPhi         = cms.double( 3.14159265359),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('none'),
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single prompt muon flatOneOverPt 2-200 GeV -1.6<eta<-1.2 transition guard PU200'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
