"""
C7_disp_pt2to5_overlap — Single Displaced Muon, OMTF overlap region, no PU.
pT-binned controlled sample for prompt vs displaced feature discrimination study.

Key properties:
  n muons/event: 1  (charge randomised)
  pT: flat in 1/pT over [2, 5] GeV  (controlled pT bin)
  eta: 0.82 < |eta| < 1.24  (OMTF overlap acceptance)
  phi: full 2pi
  displacement: displaced (|d0| 0.2–50 cm, flat uniform)
  PU: none

Purpose:
  Matched displaced signal for feature discrimination vs prompt at same pT/eta.
  Part of WP4 feature-discrimination study (Section 8, new_datset_reqs.md).
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID         = cms.vint32(-13),
        MinPt          = cms.double(2),
        MaxPt          = cms.double(5),
        MinDxy         = cms.double(0.2),            # displaced: |d0| >= 0.2 cm
        MaxDxy         = cms.double(50.0),           # flat d0 up to 50 cm
        MaxLxy         = cms.double(200.0),          # keep vertex inside MB1
        MinEta         = cms.double(-1.24),
        MaxEta         = cms.double( 1.24),
        MinPhi         = cms.double(-3.14159265359),
        MaxPhi         = cms.double( 3.14159265359),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('flatD0'),
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single displaced muon flatOneOverPt 2-5 GeV d0 0.2-50cm OMTF overlap'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

etaFilter = cms.EDFilter("MCSingleParticleFilter",
    ParticleID  = cms.untracked.vint32(13, -13, 13, -13),
    Status      = cms.untracked.vint32(1, 1, 1, 1),
    MinPt       = cms.untracked.vdouble(0.0, 0.0, 0.0, 0.0),
    MinEta      = cms.untracked.vdouble(0.82, 0.82, -1.24, -1.24),
    MaxEta      = cms.untracked.vdouble(1.24, 1.24, -0.82, -0.82),
)

ProductionFilterSequence = cms.Sequence(generator * etaFilter)
