import ROOT, glob, numpy as np
ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError

EOS = "/eos/user/p/pleguina/omtf_hecin_datasets/prod"

# Check G3 with 200 files
sample_h = sorted(glob.glob(f"{EOS}/G3/omtf_hits_G3_*.root"))[:200]
rdf = ROOT.RDataFrame("simOmtfPhase2Digis/OMTFHitsTree", ROOT.std.vector("string")(sample_h))
eta = np.asarray(rdf.AsNumpy(["muonEta"])["muonEta"])
print(f"G3 OMTFHitsTree (200 files, {len(eta)} entries):")
print(f"  eta min={eta.min():.3f}  max={eta.max():.3f}  mean={eta.mean():.3f}")
print(f"  positive eta: {(eta>0).mean():.1%}   negative eta: {(eta<0).mean():.1%}")

sample_n = sorted(glob.glob(f"{EOS}/G3/omtf_nano_G3_*.root"))[:200]
rdf_n = ROOT.RDataFrame("Events", ROOT.std.vector("string")(sample_n))
gen_eta_raw = rdf_n.AsNumpy(["GenMuon_eta"])["GenMuon_eta"]
gen_eta = np.concatenate([np.asarray(v, dtype=float) for v in gen_eta_raw if len(v)])
print(f"\nG3 NanoAOD GenMuon_eta (200 files, {len(gen_eta)} muons):")
print(f"  eta min={gen_eta.min():.3f}  max={gen_eta.max():.3f}  mean={gen_eta.mean():.3f}")
print(f"  positive: {(gen_eta>0).mean():.1%}   negative: {(gen_eta<0).mean():.1%}")

# Compare with G1
print()
sample_h1 = sorted(glob.glob(f"{EOS}/G1/omtf_hits_G1_*.root"))[:200]
rdf1 = ROOT.RDataFrame("simOmtfPhase2Digis/OMTFHitsTree", ROOT.std.vector("string")(sample_h1))
eta1 = np.asarray(rdf1.AsNumpy(["muonEta"])["muonEta"])
print(f"G1 OMTFHitsTree (200 files, {len(eta1)} entries):")
print(f"  positive: {(eta1>0).mean():.1%}   negative: {(eta1<0).mean():.1%}")

# Check the G3 fragment to understand eta range
print("\nChecking G3 fragment etaFilter...")
import subprocess
result = subprocess.run(
    ["grep", "-A30", "etaFilter", 
     "/afs/cern.ch/user/p/pleguina/CMSSW_14_2_0_pre2/src/Configuration/GenProduction/python/OMTF_HECIN/G3_fragment.py"],
    capture_output=True, text=True
)
print(result.stdout[:1000])
