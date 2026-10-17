"""
B4_noHardScatter_PU200 — PU200 only, no hard-scatter muon (rate / fake sample).

The hard scatter is a single neutrino (PartID 14), so every OMTF candidate comes
from pileup. Recovered from the prodv2 configs/B4_cfg.py generator block.
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomOneOverPtGunProducer",
    PGunParameters = cms.PSet(
        PartID       = cms.vint32(14),
        MinOneOverPt = cms.double(0.005),
        MaxOneOverPt = cms.double(0.5),
        MinEta       = cms.double(-1.24),
        MaxEta       = cms.double( 1.24),
        MinPhi       = cms.double(-3.14159265359),
        MaxPhi       = cms.double( 3.14159265359),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('noise-only PU200 (neutrino gun, no hard-scatter muon)'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
