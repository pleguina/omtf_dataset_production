"""
C16_disp_pt5to10_overlap_PU200 — Single Displaced Muon, OMTF overlap region, PU200.
pT-binned controlled sample for displaced recovery validation at realistic occupancy.

Key properties:
  n muons/event: 1  (charge randomised)
  pT: flat in 1/pT over [5, 10] GeV  (controlled pT bin)
  eta: 0.82 < |eta| < 1.24  (OMTF overlap acceptance)
  phi: full 2pi
  displacement: displaced (|d0| 0.2–50 cm, flat uniform)
  PU: 200

Purpose:
  PU200 displaced signal for ML model validation at realistic occupancy.
  Part of WP4/WP5/WP6 (new_datset_reqs.md).
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID         = cms.vint32(-13),
        MinPt          = cms.double(5),
        MaxPt          = cms.double(10),
        MinDxy         = cms.double(0.2),
        MaxDxy         = cms.double(50.0),
        MaxLxy         = cms.double(200.0),
        MinEta         = cms.double(-1.24),
        MaxEta         = cms.double( 1.24),
        MinPhi         = cms.double(-3.14159265359),
        MaxPhi         = cms.double( 3.14159265359),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('flatD0'),
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single displaced muon flatOneOverPt 5-10 GeV d0 0.2-50cm OMTF overlap PU200'),
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
