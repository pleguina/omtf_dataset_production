"""
G6 — Three Prompt Muons, OMTF overlap region, PU200.

GMT-visible-stub campaign: 3 prompt candidates under PU200 (slot-0, slot-1,
slot-2 positive targets). Provides 3-candidate PU supervision missing from S4.

Key properties:
  n muons/event: 3  (three entries in PartID — intentional multi-muon topology)
  pT: flat in 1/pT over [5, 80] GeV per muon
  eta: positive OMTF overlap [0.82, 1.24] — all three in same processor window
  phi: [-pi/6, +pi/6] — 60-degree sector centred at phi=0
       maximises probability of overlapping stubs (slot-2 stress test)
  displacement: prompt (d0 = 0)
  PU: 200 (added via mixer in DIGI step via cmsDriver --pileup AVE_200_BX_25ns)

Multi-muon note:
  PartID = [-13, 13, -13]: three independent muons per event (intended).
  RandomCharge=True independently randomises each charge.

Production target: 200,000 events / 400 jobs at 500 events/job.
"""
import FWCore.ParameterSet.Config as cms
import math

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID         = cms.vint32(-13, 13, -13),   # 3 muons; charges independently randomised
        MinPt          = cms.double(5.0),             # [GeV] pT range per muon
        MaxPt          = cms.double(80.0),
        MinDxy         = cms.double(0.0),             # prompt: d0 = 0
        MaxDxy         = cms.double(0.0),
        MinEta         = cms.double(0.82),            # positive OMTF overlap — same processor
        MaxEta         = cms.double(1.24),
        MinPhi         = cms.double(-math.pi / 6),   # 60-deg sector centred at phi=0
        MaxPhi         = cms.double( math.pi / 6),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('none'),          # prompt vertex
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('three prompt muons flatOneOverPt 5-80 GeV OMTF overlap same-sector PU200'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
