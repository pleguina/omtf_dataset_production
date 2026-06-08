"""
G17 — Two Displaced Muons, OMTF overlap region, no PU.

Clean two-displaced-muon sample without pileup.
Complements G5 (two displaced muons + PU200) with a pure-geometry version.

Key properties:
  n muons/event: 2  (two independent muons, charges randomised)
  pT: flat in 1/pT over [2, 200] GeV per muon
  eta: positive OMTF overlap [0.82, 1.24] — guarantees same processor window
  phi: [-pi/3, +pi/3] — 120-degree OMTF processor window
  displacement: flat d0 in [0, 30] cm per muon; MaxLxy = 200 cm
  PU: none

Purpose:
  Clean displaced dimuon sample for OC cluster separation study without PU.
  Allows clean geometry-level study before adding PU complexity.
  Part of WP4/WP5 (new_datset_reqs.md Sec 5.3).
"""
import FWCore.ParameterSet.Config as cms
import math

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID         = cms.vint32(-13, 13),        # 2 muons; charges independently randomised
        MinPt          = cms.double(2.0),
        MaxPt          = cms.double(200.0),
        MinDxy         = cms.double(0.0),
        MaxDxy         = cms.double(30.0),           # flat d0 0–30 cm
        MaxLxy         = cms.double(200.0),
        MinEta         = cms.double(0.82),
        MaxEta         = cms.double(1.24),
        MinPhi         = cms.double(-math.pi / 3),
        MaxPhi         = cms.double( math.pi / 3),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('flatD0'),
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('two displaced muons flatOneOverPt flatD0 d0 0-30cm OMTF overlap no PU'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
