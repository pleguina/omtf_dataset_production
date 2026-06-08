"""
G5 — Two Displaced Muons, OMTF overlap region, PU200.

GMT-visible-stub campaign: 2 displaced candidates under PU200 (slot-0 and slot-1
positive targets). Provides displaced dimuon PU supervision missing from S5.

Key properties:
  n muons/event: 2  (two entries in PartID — intentional multi-muon topology)
  pT: flat in 1/pT over [2, 200] GeV per muon
  eta: positive OMTF overlap [0.82, 1.24] — guarantees same processor window
  phi: [-pi/3, +pi/3] — 120-degree processor window centred at phi=0
       increases same-processor probability to near 100%
  displacement: flat d0 in [0, 30] cm per muon; MaxLxy = 200 cm
  PU: 200 (added via mixer in DIGI step via cmsDriver --pileup AVE_200_BX_25ns)

Multi-muon note:
  PartID = [-13, 13]: two independent muons per event (mu- and mu+), each with
  independently randomised charge (RandomCharge=True) and kinematics.
  This is intentional and correct for a two-muon sample.

Production target: 200,000 events / 400 jobs at 500 events/job.
"""
import FWCore.ParameterSet.Config as cms
import math

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID         = cms.vint32(-13, 13),        # 2 muons; charges independently randomised
        MinPt          = cms.double(2.0),             # [GeV] pT range per muon
        MaxPt          = cms.double(200.0),
        MinDxy         = cms.double(0.0),             # [cm] d0 range, flat uniform
        MaxDxy         = cms.double(30.0),            # [cm] OC stress-test zone
        MaxLxy         = cms.double(200.0),           # [cm] keep vertex inside MB1 (r < 231 cm)
        MinEta         = cms.double(0.82),            # positive OMTF overlap — same processor
        MaxEta         = cms.double(1.24),
        MinPhi         = cms.double(-math.pi / 3),   # 120-deg processor window centred at phi=0
        MaxPhi         = cms.double( math.pi / 3),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('flatD0'),
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('two displaced muons flatOneOverPt flatD0 d0 0-30cm OMTF overlap PU200'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
