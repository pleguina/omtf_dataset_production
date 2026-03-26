"""
S1 — Single Prompt Muon generator fragment.
Flat in 1/pT, pT range 2–200 GeV, OMTF acceptance 0.82 < |eta| < 1.24.
50% mu+, 50% mu− via charge randomisation.
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomOneOverPtGunProducer",
    PGunParameters = cms.PSet(
        PartID   = cms.vint32(-13, 13),   # mu+ and mu-
        MinOneOverPt = cms.double(1.0 / 200.0),  # = 0.005
        MaxOneOverPt = cms.double(1.0 / 2.0),    # = 0.5
        MinEta   = cms.double(-1.24),
        MaxEta   = cms.double(1.24),
        MinPhi   = cms.double(-3.14159265359),
        MaxPhi   = cms.double(3.14159265359),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single muon flat 1/pT 2-200'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

# Eta filter: keep only events where generated muon is in OMTF acceptance
etaFilter = cms.EDFilter("MCParticleModuloFilter",
    moduleLabel = cms.InputTag("generator", "unsmeared"),
    minEta  = cms.double(0.82),
    maxEta  = cms.double(1.24),
    absetaMode = cms.bool(True),
    status  = cms.int32(1),
    particleID = cms.vint32(13, -13),
)

ProductionFilterSequence = cms.Sequence(generator)
