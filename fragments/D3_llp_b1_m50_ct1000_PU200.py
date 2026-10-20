"""
D3_llp_b1_m50_ct1000_PU200 -- LLP pair decaying to muons, decay position in bin 1 (between stations 1 and 2), PU200.
FlatRandomLLPGunProducer2 (EMTFTools/ParticleGuns): H -> 2 LLPs, each LLP -> mu+ mu-.
  H mass:     125 GeV (fixed)
  LLP mass:   50 GeV (fixed)
  H pT:       1 - 100 GeV (flat in 1/pT);  H |eta| 0 - 1.6
  LLP c*tau:  1000 mm (fixed)
  Decay bin:  1 along the LLP flight direction relative to the overlap stations (barrel cylinders r = 4000/4900/5900 mm,
              endcap disks |z| = 6900/8300/9250 mm): 0 before station 1, 1 between 1 and 2, 2 between 2 and 3, 3 beyond.
              The decay length is drawn from the exponential truncated to the bin, so the sample is bin-conditional
              (not natural rates); both LLPs of an event are in the bin.
  Filter:     at least one muon with pT >= 2 GeV and 0.6 < |eta| < 1.4 at the vertex
The decay vertex carries the LLP flight time (T = gamma * proper time), so SimHit times include the decay delay.
LLP, H and decay-time truth is written to the llpTruth product (GenMuon_llp* columns).
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomLLPGunProducer2",
    PGunParameters = cms.PSet(
        PartID      = cms.vint32(-13),
        MinMassH    = cms.double(125),
        MaxMassH    = cms.double(125),
        MinMassLLP  = cms.double(50),
        MaxMassLLP  = cms.double(50),
        MinPtH      = cms.double(1),
        MaxPtH      = cms.double(100),
        MinCTauLLP  = cms.double(1000),
        MaxCTauLLP  = cms.double(1000),
        MinEta      = cms.double(0.0),
        MaxEta      = cms.double(1.6),
        MinPhi      = cms.double(-3.14159265359),
        MaxPhi      = cms.double( 3.14159265359),
        DecayBin    = cms.int32(1),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('LLP pair to muons, m=50 GeV, ctau=1000 mm, decay bin 1, OMTF overlap'),
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
