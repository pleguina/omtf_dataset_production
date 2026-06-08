"""
C28_mild_disp_pt5to10_overlap_PU200 — Single Mild-Displaced Muon, OMTF overlap region PU200.
Mild displacement (|d0| 0.05–0.2 cm): the critical ambiguity zone between
prompt-like and clearly displaced. Most dangerous fake source for displaced model.

Key properties:
  n muons/event: 1  (charge randomised)
  pT: flat in 1/pT over [5, 10] GeV  (controlled pT bin)
  eta: 0.82 < |eta| < 1.24  (OMTF overlap acceptance)
  phi: full 2pi
  displacement: mild displaced (|d0| 0.05–0.2 cm)
  PU: 200

Purpose:
  Critical ambiguity sample: muons that are mildly off-beam but not clearly
  displaced. Tests whether displaced model misidentifies these as signal.
  Part of WP4 prompt-vs-displaced discrimination study (new_datset_reqs.md Sec 5.1).
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID         = cms.vint32(-13),
        MinPt          = cms.double(5),
        MaxPt          = cms.double(10),
        MinDxy         = cms.double(0.05),           # mild displaced lower bound
        MaxDxy         = cms.double(0.2),            # mild displaced upper bound
        MaxLxy         = cms.double(50.0),
        MinEta         = cms.double(-1.24),
        MaxEta         = cms.double( 1.24),
        MinPhi         = cms.double(-3.14159265359),
        MaxPhi         = cms.double( 3.14159265359),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('flatD0'),
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single mild-displaced muon flatOneOverPt 5-10 GeV d0 0.05-0.2cm OMTF overlap PU200'),
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
