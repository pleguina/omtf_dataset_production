"""
G8 — Hard Negative: Single Low-eta (Barrel) Muon, PU200.

GMT-visible-stub campaign: real KMTF-like barrel muon outside OMTF-overlap
task, under PU200. Probably the single most important new hard-negative dataset
for the GMT-visible-stub branch.

Key properties:
  n muons/event: 1  (PartID has exactly ONE entry; charge randomised by gun)
  pT: flat in 1/pT over [2, 200] GeV -> unbiased curvature coverage
  eta: |eta| < 0.75  (KMTF barrel, strictly outside OMTF overlap acceptance)
  phi: full 2pi
  displacement: prompt (d0 = 0)
  PU: 200 (added via mixer in DIGI step via cmsDriver --pileup AVE_200_BX_25ns)

Model target: no overlap candidate. G8 directly addresses the current B2
false-candidate-from-PU problem: trains the model to reject coherent KMTF-like
noise plus a real low-eta muon track.

Not a replacement for B4 (pure noise). G8 contains real KMTF-like stubs with
PU clutter, but the overlap target count should remain zero.

Charge note:
  PartID = [-13] (mu-) with RandomCharge=True -> ~50% mu+ / ~50% mu-.
  Exactly one PDG ID entry satisfies the single-muon rule.

Production target: 300,000 events / 600 jobs at 500 events/job.
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
    psethack        = cms.string('single barrel muon flatOneOverPt 2-200 GeV |eta|<0.75 hard-negative PU200'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
