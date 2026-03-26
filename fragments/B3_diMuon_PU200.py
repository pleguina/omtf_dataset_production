"""
B3 - DiMuon Prompt + PU200, same OMTF processor window.

Same generator configuration as S3, with PU200 added by the cfg mixer.

Both muons constrained to phi in [-pi/6, +pi/6], eta in [0.82, 1.24].
100% same-window efficiency by construction.
"""
import FWCore.ParameterSet.Config as cms
import math

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID          = cms.vint32(-13, 13),
        MinPt           = cms.double(2.0),
        MaxPt           = cms.double(100.0),
        MinDxy          = cms.double(0.0),
        MaxDxy          = cms.double(0.0),
        MinEta          = cms.double(0.82),
        MaxEta          = cms.double(1.24),
        MinPhi          = cms.double(-math.pi / 6),
        MaxPhi          = cms.double( math.pi / 6),
        PtSpectrum      = cms.string('flatOneOverPt'),
        VertexSpectrum  = cms.string('none'),
        RandomCharge    = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('dimuon prompt same phi-sector flatOneOverPt 2-100 GeV PU200'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
