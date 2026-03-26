"""
S3 - DiMuon Prompt, same OMTF processor window.

Uses FlatRandomPtGunProducer2 (EMTFTools/ParticleGuns, already compiled).

Strategy to enforce same-window:
  Constrain both muons' phi to a single 120-degree processor window
  (sector 0, centered at phi=0: MinPhi=-pi/3, MaxPhi=+pi/3) and restrict
  eta to the positive OMTF endcap [0.82, 1.24]. Both muons are
  in OMTF processor sector 0 by construction -- 100% same-window
  efficiency, no post-filter or overhead.

  OMTF geometry (Phase 2, hwToLogicLayer_0x0209.xml):
    nProcessors = 3 per eta side  =>  each processor covers 120 deg (2*pi/3).
    processorCnt = 6 total (3 positive + 3 negative eta).
    Processor 0 centre: phi = 0, range: [-pi/3, +pi/3].

  Since OMTF has 3-fold phi symmetry and stub phi is stored in processor-
  local coordinates, training on sector-0 events only is fully equivalent
  to training on all sectors.

Delta_phi distribution:
  Both muons phi independently uniform in [-pi/3, +pi/3].
  delta_phi = phi_1 - phi_2 follows a triangular distribution on [0, 2*pi/3].
  Mean |delta_phi| ~ pi/3 ~ 1.05 rad. Naturally close-pair enriched
  (more events with small delta_phi), which is what OC training needs.

pT: flat in 1/pT over [2, 100] GeV per muon -- no reweighting needed.
Charge: independently randomised per muon (RandomCharge=True).
Vertex: prompt (VertexSpectrum='none').
eta: positive OMTF endcap only [0.82, 1.24] -- same processor guaranteed.
PU: 0
Target events: 250,000
"""
import FWCore.ParameterSet.Config as cms
import math

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID          = cms.vint32(-13, 13),       # mu- and mu+; charges independently randomised
        MinPt           = cms.double(2.0),            # [GeV] pT range
        MaxPt           = cms.double(100.0),
        MinDxy          = cms.double(0.0),            # prompt: d0 = 0
        MaxDxy          = cms.double(0.0),
        MinEta          = cms.double(0.82),           # positive OMTF endcap only
        MaxEta          = cms.double(1.24),           # -- guarantees same processor
        MinPhi          = cms.double(-math.pi / 3),  # 120-deg processor window centred at phi=0
        MaxPhi          = cms.double( math.pi / 3),  # both muons always in sector 0
        PtSpectrum      = cms.string('flatOneOverPt'),
        VertexSpectrum  = cms.string('none'),         # prompt vertex
        RandomCharge    = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('dimuon prompt same 120deg phi-sector flatOneOverPt 2-100 GeV'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
