"""
D1 — Displaced muon via LLP gun, full OMTF acceptance (|eta| < 1.24), no PU.

Uses FlatRandomLLPGunProducer2 from EMTFTools/ParticleGuns (installed by setup_cmssw.sh).

Event topology: H-like parent -> 2 LLPs, each LLP -> 1 muon (PartID).
With PartID = [-13, 13], two H bosons are produced per event, giving two
displaced mu- and two displaced mu+ (each from an independent H->LLP chain).

Parameter ranges (wide, flat):
  H parent mass:  20 – 1000 GeV  (flat)
  H parent pT:     1 – 200  GeV  (flat in 1/pT via MinPtH/MaxPtH)
  LLP ctau:        1 – 10000 mm  (flat, ~0.1 cm to 10 m)
  Muon eta:   -1.24 – 1.24       (full OMTF acceptance, both endcaps)
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomLLPGunProducer2",
    PGunParameters = cms.PSet(
        PartID      = cms.vint32(-13, 13),      # mu- and mu+ (one LLP chain each)
        MinMassH    = cms.double(20),            # H-like parent mass range [GeV]
        MaxMassH    = cms.double(1000),
        MinPtH      = cms.double(1),             # H-like parent pT range [GeV]
        MaxPtH      = cms.double(200),
        MinCTauLLP  = cms.double(1),             # LLP proper decay length [mm]
        MaxCTauLLP  = cms.double(10000),
        MinEta      = cms.double(-1.24),         # full OMTF eta acceptance
        MaxEta      = cms.double( 1.24),
        MinPhi      = cms.double(-3.14159265359),
        MaxPhi      = cms.double( 3.14159265359),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('displaced muon via LLP flat ctau, OMTF'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

ProductionFilterSequence = cms.Sequence(generator)
