"""
B1 — Single Prompt Muon + PU200.
Same kinematics as S1, with 200 pileup interactions.
50% events pT in [2,10], 50% pT in [2,200] for low-pT coverage.
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomOneOverPtGunProducer",
    PGunParameters = cms.PSet(
        PartID   = cms.vint32(-13, 13),
        MinOneOverPt = cms.double(1.0 / 200.0),
        MaxOneOverPt = cms.double(1.0 / 2.0),
        MinEta   = cms.double(-1.24),
        MaxEta   = cms.double(1.24),
        MinPhi   = cms.double(-3.14159265359),
        MaxPhi   = cms.double(3.14159265359),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single muon flat 1/pT 2-200 PU200'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
