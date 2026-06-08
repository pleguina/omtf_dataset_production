"""
G15_neg_promptEndcap — Single Prompt Muon, deep endcap region (|eta| > 1.8), no PU.

Extends G9/G10 hard-negative coverage deeper into the endcap (beyond 1.80)
to cover the full CSC acceptance up to |eta| ~ 2.4.

Key properties:
  n muons/event: 1  (charge randomised)
  pT: flat in 1/pT over [2, 200] GeV
  eta: -3.0 < eta < -1.8  (deep endcap)
  phi: full 2pi
  displacement: prompt (d0 = 0)
  PU: none

Purpose:
  Hard-negative deep-endcap sample.  Real CSC-heavy endcap muons that should
  NOT produce an OMTF overlap candidate.
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
        MinEta         = cms.double(-3.0),
        MaxEta         = cms.double(-1.8),
        MinPhi         = cms.double(-3.14159265359),
        MaxPhi         = cms.double( 3.14159265359),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('none'),
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single prompt muon flatOneOverPt 2-200 GeV -3.0<eta<-1.8 deep endcap'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
