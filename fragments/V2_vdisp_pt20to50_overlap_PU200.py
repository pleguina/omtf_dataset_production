"""
V2_vdisp_pt20to50_overlap_PU200 -- very displaced single muon, PU200.
Key properties:
  n muons/event: 1  (charge randomised)
  pT: flat in 1/pT over [20, 50] GeV
  eta: gun |eta| < 1.4; the filter keeps 0.6 < |eta| < 1.4 at the vertex (overlap selected on GenMuon_etaSt2 in analysis)
  phi: full 2pi
  displacement: helix perigee |d0| 50-150 cm (flat), production vertex Lxy flat from |d0| up to 500 cm
                (the earlier displaced guns stop at |d0| 50 cm and Lxy 200 cm)
  PU: PU200
Purpose: training and efficiency range beyond |d0| = 50 cm (WP2). The vertex time is left at t = 0
(single-muon guns do not model a decay time), so geometry stays separate from timing.
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID         = cms.vint32(-13),
        MinPt          = cms.double(20),
        MaxPt          = cms.double(50),
        MinDxy         = cms.double(50.0),
        MaxDxy         = cms.double(150.0),
        MaxLxy         = cms.double(500.0),
        MinEta         = cms.double(-1.4),
        MaxEta         = cms.double( 1.4),
        MinPhi         = cms.double(-3.14159265359),
        MaxPhi         = cms.double( 3.14159265359),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('flatD0'),
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single very displaced muon flatOneOverPt 20-50 GeV d0 50-150cm Lxy<=500cm OMTF overlap PU200'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

etaFilter = cms.EDFilter("MCSingleParticleFilter",
    ParticleID  = cms.untracked.vint32(13, -13, 13, -13),
    Status      = cms.untracked.vint32(1, 1, 1, 1),
    MinPt       = cms.untracked.vdouble(0.0, 0.0, 0.0, 0.0),
    MinEta      = cms.untracked.vdouble(0.6, 0.6, -1.4, -1.4),
    MaxEta      = cms.untracked.vdouble(1.4, 1.4, -0.6, -0.6),
)

ProductionFilterSequence = cms.Sequence(generator * etaFilter)
