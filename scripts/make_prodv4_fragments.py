#!/usr/bin/env python3
"""Generate the prodv4 generator fragments (OMTF_HECIN) and write them to fragments/ and, optionally, to a CMSSW area.

Usage: make_prodv4_fragments.py [--area ~/CMSSW_20_0_0_patch2_prodv4]

prodv4 datasets defined here (C13-C16 and B4 reuse the existing fragments):
  V1-V3  very displaced single muon, |d0| 50-150 cm, Lxy <= 500 cm, pT 10-20 / 20-50 / 50-100 GeV, PU200
  V4-V6  same, no pileup
  D3     LLP -> mu mu with the decay position binned relative to the overlap stations
         (bin 0 before station 1, 1 between 1 and 2, 2 between 2 and 3, 3 beyond), mH = 125 GeV,
         m(LLP) 15 / 50 GeV, c*tau 100 / 1000 mm, PU200
D4 (multi-BX) datasets reuse the D3 m50 ct1000 configs; they are produced by copying the config (generate_configs_prodv4.sh).
"""
import argparse, os

HDR = '''import FWCore.ParameterSet.Config as cms
'''

GUN_TMPL = '''"""
{name} -- very displaced single muon, {pu_text}.
Key properties:
  n muons/event: 1  (charge randomised)
  pT: flat in 1/pT over [{ptlo}, {pthi}] GeV
  eta: gun |eta| < 1.4; the filter keeps 0.6 < |eta| < 1.4 at the vertex (overlap selected on GenMuon_etaSt2 in analysis)
  phi: full 2pi
  displacement: helix perigee |d0| 50-150 cm (flat), production vertex Lxy flat from |d0| up to 500 cm
                (the earlier displaced guns stop at |d0| 50 cm and Lxy 200 cm)
  PU: {pu_short}
Purpose: training and efficiency range beyond |d0| = 50 cm (WP2). The vertex time is left at t = 0
(single-muon guns do not model a decay time), so geometry stays separate from timing.
"""
import FWCore.ParameterSet.Config as cms

generator = cms.EDProducer("FlatRandomPtGunProducer2",
    PGunParameters = cms.PSet(
        PartID         = cms.vint32(-13),
        MinPt          = cms.double({ptlo}),
        MaxPt          = cms.double({pthi}),
        MinDxy         = cms.double(50.0),
        MaxDxy         = cms.double(150.0),
        MaxLxy         = cms.double(500.0),
        MinEta         = cms.double(-1.4),
        MaxEta         = cms.double( 1.4),
        MinPhi         = cms.double(-3.14159265359),
        MaxPhi         = cms.double( 3.14159265359),
        PtSpectrum     = cms.string('flatOneOverPt'),
        VertexSpectrum = cms.string('flatD0'),
        RandomCharge   = cms.bool(True),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('single very displaced muon flatOneOverPt {ptlo}-{pthi} GeV d0 50-150cm Lxy<=500cm OMTF overlap {pu_short}'),
    AddAntiParticle = cms.bool(False),
    firstRun        = cms.untracked.uint32(1),
)

etaFilter = cms.EDFilter("MCSingleParticleFilter",
    ParticleID  = cms.untracked.vint32(13, -13, 13, -13),
    Status      = cms.untracked.vint32(1, 1, 1, 1),
    MinPt       = cms.untracked.vdouble(0.0, 0.0, 0.0, 0.0),
    MinEta      = cms.untracked.vdouble(0.6, 0.6, -1.4, -1.4),
    MaxEta      = cms.untracked.vdouble(1.4, 1.4, -0.6, -0.6),
)

ProductionFilterSequence = cms.Sequence(generator * etaFilter)
'''

LLP_TMPL = '''"""
{name} -- LLP pair decaying to muons, decay position in bin {b} ({bintext}), PU200.
FlatRandomLLPGunProducer2 (EMTFTools/ParticleGuns): H -> 2 LLPs, each LLP -> mu+ mu-.
  H mass:     125 GeV (fixed)
  LLP mass:   {m} GeV (fixed)
  H pT:       1 - 100 GeV (flat in 1/pT);  H |eta| 0 - 1.6
  LLP c*tau:  {ct} mm (fixed)
  Decay bin:  {b} along the LLP flight direction relative to the overlap stations (barrel cylinders r = 4000/4900/5900 mm,
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
        MinMassLLP  = cms.double({m}),
        MaxMassLLP  = cms.double({m}),
        MinPtH      = cms.double(1),
        MaxPtH      = cms.double(100),
        MinCTauLLP  = cms.double({ct}),
        MaxCTauLLP  = cms.double({ct}),
        MinEta      = cms.double(0.0),
        MaxEta      = cms.double(1.6),
        MinPhi      = cms.double(-3.14159265359),
        MaxPhi      = cms.double( 3.14159265359),
        DecayBin    = cms.int32({b}),
    ),
    Verbosity       = cms.untracked.int32(0),
    psethack        = cms.string('LLP pair to muons, m={m} GeV, ctau={ct} mm, decay bin {b}, OMTF overlap'),
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
'''

BINTEXT = ["before station 1", "between stations 1 and 2", "between stations 2 and 3", "beyond station 3"]

TEST_TMPL = LLP_TMPL.replace("PU200.\nFlatRandom", "no PU.\nFlatRandom").replace(
    "        DecayBin    = cms.int32({b}),\n", "        DecayBin    = cms.int32({b}),\n{extra}")

def test_datasets():
    """Smoke-test fragments (no PU): vertex-time A/B (T kept / T zeroed) for the LLP gun, bin 1, m50, ct1000."""
    out = {}
    for n, extra in (("T1_llp_b1_nopu", ""), ("T2_llp_b1_nopu_zeroT", "        ZeroDecayVertexTime = cms.bool(True),\n")):
        out[n] = TEST_TMPL.format(name=n, b=1, bintext=BINTEXT[1], m=50, ct=1000, extra=extra)
    return out

def datasets():
    out = {}
    for i, (lo, hi) in enumerate([(10, 20), (20, 50), (50, 100)]):
        out[f"V{i+1}_vdisp_pt{lo}to{hi}_overlap_PU200"] = GUN_TMPL.format(
            name=f"V{i+1}_vdisp_pt{lo}to{hi}_overlap_PU200", ptlo=lo, pthi=hi, pu_text="PU200", pu_short="PU200")
        out[f"V{i+4}_vdisp_pt{lo}to{hi}_overlap"] = GUN_TMPL.format(
            name=f"V{i+4}_vdisp_pt{lo}to{hi}_overlap", ptlo=lo, pthi=hi, pu_text="no PU", pu_short="no PU")
    for b in range(4):
        for m in (15, 50):
            for ct in (100, 1000):
                n = f"D3_llp_b{b}_m{m}_ct{ct}_PU200"
                out[n] = LLP_TMPL.format(name=n, b=b, bintext=BINTEXT[b], m=m, ct=ct)
    return out

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--area", default=None)
    ap.add_argument("--tests", action="store_true", help="write the smoke-test fragments (T*) instead")
    a = ap.parse_args()
    here = os.path.dirname(os.path.abspath(__file__))
    outs = [os.path.join(here, "..", "fragments")]
    if a.area:
        outs.append(os.path.join(os.path.expanduser(a.area), "src/Configuration/GenProduction/python/OMTF_HECIN"))
    ds = test_datasets() if a.tests else datasets()
    for d in outs:
        os.makedirs(d, exist_ok=True)
        for n, t in ds.items():
            open(os.path.join(d, n + ".py"), "w").write(t)
    print(len(ds), "fragments written to", [os.path.normpath(d) for d in outs])
