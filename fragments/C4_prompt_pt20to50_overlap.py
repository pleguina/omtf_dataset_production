"""
C4_prompt_pt20to50_overlap — Single Prompt Muon, OMTF overlap region, no PU.
pT-binned controlled sample for prompt vs displaced feature discrimination study.

Key properties:
  n muons/event: 1  (charge randomised)
  pT: flat in 1/pT over [20, 50] GeV  (controlled pT bin)
  eta: 0.82 < |eta| < 1.24  (OMTF overlap acceptance)
  phi: full 2pi
  displacement: prompt-like (|d0| < 0.05 cm)
  PU: none

Purpose:
  Matched prompt control for displaced discrimination at same pT and eta.
  Part of WP4 feature-discrimination study (Section 8, new_datset_reqs.md).
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID         = cms.vint32(-13),
        MinPt          = cms.double(20),
        MaxPt          = cms.double(50),
        MinDxy         = cms.double(0.0),
        MaxDxy         = cms.double(0.05),           # prompt-like: |d0| < 0.05 cm
        MaxLxy         = cms.double(10.0),
        MinEta         = cms.double(-1.24),
        MaxEta         = cms.double( 1.24),
        MinPhi         = cms.double(-3.14159265359),
        MaxPhi         = cms.double( 3.14159265359),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('flatD0'),
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single prompt muon flatOneOverPt 20-50 GeV d0<0.05cm OMTF overlap'),
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
