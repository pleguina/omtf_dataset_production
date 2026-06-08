"""
G7 — Hard Negative: Single Low-eta (Barrel) Muon, no PU.

GMT-visible-stub campaign: real KMTF-like barrel muon outside the OMTF-overlap
task. Correct training target for Phase-B GMT-overlap branch: no overlap candidate.

Key properties:
  n muons/event: 1  (PartID has exactly ONE entry; charge randomised by gun)
  pT: flat in 1/pT over [2, 200] GeV -> unbiased curvature coverage
  eta: |eta| < 0.75  (KMTF barrel, strictly outside OMTF overlap acceptance)
  phi: full 2pi
  displacement: prompt (d0 = 0)
  PU: none

Model target: no overlap candidate. This sample teaches the model that a
real-looking KMTF barrel track outside the OMTF-overlap acceptance should not
produce an overlap candidate.

Not a replacement for B4 (pure noise). G7 contains real KMTF-like stubs but
the overlap target count should be zero.

Charge note:
  PartID = [-13] (mu-) with RandomCharge=True -> ~50% mu+ / ~50% mu-.
  Exactly one PDG ID entry satisfies the single-muon rule.

Production target: 150,000 events / 300 jobs at 500 events/job.
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID         = cms.vint32(-13),            # single muon; charge randomised below
        MinPt          = cms.double(2.0),             # [GeV] pT range
        MaxPt          = cms.double(200.0),
        MinDxy         = cms.double(0.0),             # prompt: d0 = 0
        MaxDxy         = cms.double(0.0),
        MinEta         = cms.double(-0.75),           # KMTF barrel, outside OMTF overlap
        MaxEta         = cms.double( 0.75),
        MinPhi         = cms.double(-3.14159265359),
        MaxPhi         = cms.double( 3.14159265359),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('none'),          # prompt vertex
        RandomCharge   = cms.bool(True),              # 50% mu+ / 50% mu- per event
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single barrel muon flatOneOverPt 2-200 GeV |eta|<0.75 hard-negative'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
