"""
D2_llp_overlap_PU200 — LLP pair decays into muons, overlap-focused, PU200 added at DIGI.

FlatRandomLLPGunProducer2 (EMTFTools/ParticleGuns): one H-like parent per PartID entry,
H -> 2 LLPs, each LLP -> mu+ mu-. With PartID = [-13] that is 4 muons per event.
The η range is applied to |η| of the H (the sign is random).

  H mass:   20 – 200 GeV (flat), LLP mass flat in [10 GeV, mH/2]
  H pT:      1 – 100 GeV (flat in 1/pT)
  H |eta|:   0 – 1.6
  LLP ctau:  1 – 1000 mm (flat); with the boost most muons start within MB1 (r < 4 m)
  Filter:    at least one muon with 0.6 < |eta| < 1.4 at the vertex
             (overlap selected on GenMuon_etaSt2 in analysis)

Purpose: LLP-like displaced muons (correlated pairs, real Lxy/Lz/time-of-flight) for the
go/no-go and for training, replacing the Phase2Spring24 HTo2LongLived DAS samples, which
CMSSW_20_0_X cannot read. D1 (cτ up to 10 m, 8 muons/event) puts ~40% of its muons beyond MB1.
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomLLPGunProducer2",
    PGunParameters = cms.PSet(
        PartID      = cms.vint32(-13),
        MinMassH    = cms.double(20),
        MaxMassH    = cms.double(200),
        MinPtH      = cms.double(1),
        MaxPtH      = cms.double(100),
        MinCTauLLP  = cms.double(1),
        MaxCTauLLP  = cms.double(1000),
        MinEta      = cms.double(0.0),
        MaxEta      = cms.double(1.6),
        MinPhi      = cms.double(-3.14159265359),
        MaxPhi      = cms.double( 3.14159265359),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('LLP pair to muons, ctau 1-1000 mm, OMTF overlap'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

etaFilter = cms.EDFilter("MCSingleParticleFilter",
    ParticleID  = cms.untracked.vint32(13, -13, 13, -13),
    Status      = cms.untracked.vint32(1, 1, 1, 1),
    MinPt       = cms.untracked.vdouble(2.0, 2.0, 2.0, 2.0),
    MinEta      = cms.untracked.vdouble(0.6, 0.6, -1.4, -1.4),
    MaxEta      = cms.untracked.vdouble(1.4, 1.4, -0.6, -0.6),
)

ProductionFilterSequence = cms.Sequence(generator * etaFilter)
