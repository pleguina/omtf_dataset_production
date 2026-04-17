#!/usr/bin/env python3
"""
analyze_prod_datasets.py

Analyze all OMTF production datasets in EOS (prod), generate:
- per-dataset PDF plots only (no mixed cross-dataset plots)
- textual quality report (PASS/WARN/FAIL hints)

Run (inside CMSSW env with ROOT available):
  cd /afs/cern.ch/user/p/pleguina/CMSSW_14_2_0_pre2/src
  eval "$(scramv1 runtime -sh)"
  python3 /afs/cern.ch/user/p/pleguina/omtf_hecin_dataset_production/analysis/analyze_prod_datasets.py
"""

import os
import glob
from collections import OrderedDict

os.environ["DISPLAY"] = ""

import ROOT
ROOT.gROOT.SetBatch(True)
ROOT.gErrorIgnoreLevel = ROOT.kError

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


EOS_BASE = "/eos/user/p/pleguina/omtf_hecin_datasets/prod"
HITS_TREE_PATH = "simOmtfPhase2Digis/OMTFHitsTree"
NANO_TREE_PATH = "Events"
OUT_DIR = "/afs/cern.ch/user/p/pleguina/omtf_hecin_dataset_production/analysis"

DATASETS = OrderedDict([
    ("S1", {"label": "S1 single prompt mu", "pt": (2, 200), "max_dxy": 0, "pileup": 0}),
    ("S2", {"label": "S2 single displaced mu", "pt": (2, 200), "max_dxy": 50, "pileup": 0}),
    ("S3", {"label": "S3 prompt di-muon", "pt": (2, 100), "max_dxy": 0, "pileup": 0}),
    ("S4", {"label": "S4 prompt tri-muon", "pt": (5, 80), "max_dxy": 0, "pileup": 0}),
    ("S5", {"label": "S5 displaced di-muon", "pt": (2, 200), "max_dxy": 30, "pileup": 0}),
    ("B1", {"label": "B1 prompt mu + PU200", "pt": (2, 200), "max_dxy": 0, "pileup": 200}),
    ("B2", {"label": "B2 displaced mu + PU200", "pt": (2, 200), "max_dxy": 50, "pileup": 200}),
    ("B3", {"label": "B3 prompt di-muon + PU200", "pt": (2, 100), "max_dxy": 0, "pileup": 200}),
    ("B4", {"label": "B4 noise-only PU200", "pt": None, "max_dxy": None, "pileup": 200}),
])

EXPECTED_EVENTS = {
    "S1": 500_000,
    "S2": 500_000,
    "S3": 500_000,
    "S4": 150_000,
    "S5": 150_000,
    "B1": 200_000,
    "B2": 200_000,
    "B3": 200_000,
    "B4": 200_000,
}


def find_files(ds):
    return sorted(glob.glob(f"{EOS_BASE}/{ds}/omtf_hits_{ds}_*.root"))


def find_nano_files(ds):
    return sorted(glob.glob(f"{EOS_BASE}/{ds}/omtf_nano_{ds}_*.root"))


def _to_numpy(rdf, cols):
    arr = rdf.AsNumpy(cols)
    out = {}
    for c in cols:
        out[c] = np.asarray(arr[c])
    return out


def load_scalars(files):
    rdf = ROOT.RDataFrame(HITS_TREE_PATH, ROOT.std.vector("string")(files))
    n_entries = int(rdf.Count().GetValue())

    rdf = rdf.Define("omtfQuality_i", "static_cast<int>(omtfQuality)")
    rdf = rdf.Define("omtfScore_i", "static_cast<int>(omtfScore)")
    rdf = rdf.Define("muonCharge_i", "static_cast<int>(muonCharge)")
    rdf = rdf.Define("omtfCharge_i", "static_cast<int>(omtfCharge)")
    rdf = rdf.Define("nFired", "int n=0; for(int b=0;b<18;b++) n += ((omtfFiredLayers>>b)&1); return n;")
    rdf = rdf.Define("nHits", "static_cast<int>(hits.size())")

    cols = [
        "muonPt", "muonPropEta", "muonDxy", "muonRho",
        "omtfPt", "omtfQuality_i", "omtfScore_i",
        "muonCharge_i", "omtfCharge_i",
        "deltaEta", "deltaPhi", "nFired", "nHits"
    ]
    x = _to_numpy(rdf, cols)
    x["n_entries"] = n_entries
    return x


def _flatten_rvec_array(arr, dtype=float):
    chunks = []
    for v in arr:
        vv = np.asarray(v, dtype=dtype)
        if vv.size:
            chunks.append(vv)
    if not chunks:
        return np.array([], dtype=dtype)
    return np.concatenate(chunks)


def load_nano(nano_files):
    rdf = ROOT.RDataFrame(NANO_TREE_PATH, ROOT.std.vector("string")(nano_files))
    n_events = int(rdf.Count().GetValue())

    cols = [
        "nGenMuon", "nomtf", "nMuonStubTps", "nMuonStubKmtf",
        "GenMuon_pt", "GenMuon_eta", "GenMuon_phi",
        "GenMuon_dXY", "GenMuon_lXY", "GenMuon_etaSt1", "GenMuon_etaSt2",
        "omtf_hwPt", "omtf_hwEta", "omtf_hwPhi", "omtf_hwQual",
    ]
    x = _to_numpy(rdf, cols)

    return {
        "n_events": n_events,
        "nGenMuon": np.asarray(x["nGenMuon"], dtype=np.int32),
        "nomtf": np.asarray(x["nomtf"], dtype=np.int32),
        "nMuonStubTps": np.asarray(x["nMuonStubTps"], dtype=np.int32),
        "nMuonStubKmtf": np.asarray(x["nMuonStubKmtf"], dtype=np.int32),
        "GenMuon_pt": _flatten_rvec_array(x["GenMuon_pt"], float),
        "GenMuon_eta": _flatten_rvec_array(x["GenMuon_eta"], float),
        "GenMuon_phi": _flatten_rvec_array(x["GenMuon_phi"], float),
        "GenMuon_dXY": _flatten_rvec_array(x["GenMuon_dXY"], float),
        "GenMuon_lXY": _flatten_rvec_array(x["GenMuon_lXY"], float),
        "GenMuon_etaSt1": _flatten_rvec_array(x["GenMuon_etaSt1"], float),
        "GenMuon_etaSt2": _flatten_rvec_array(x["GenMuon_etaSt2"], float),
        "omtf_hwPt": _flatten_rvec_array(x["omtf_hwPt"], int),
        "omtf_hwEta": _flatten_rvec_array(x["omtf_hwEta"], int),
        "omtf_hwPhi": _flatten_rvec_array(x["omtf_hwPhi"], int),
        "omtf_hwQual": _flatten_rvec_array(x["omtf_hwQual"], int),
    }


def quality_checks(ds, data, nano):
    cfg = DATASETS[ds]
    n = max(1, int(data["n_entries"]))
    msgs = []

    # 1) size sanity
    exp = EXPECTED_EVENTS.get(ds, None)
    if exp is not None:
        frac = data["n_entries"] / float(exp)
        if frac < 0.5:
            msgs.append(("FAIL", f"very low entries: {data['n_entries']} ({frac:.1%} of expected {exp})"))
        elif frac < 0.9:
            msgs.append(("WARN", f"entries below target: {data['n_entries']} ({frac:.1%} of expected {exp})"))
        else:
            msgs.append(("PASS", f"entries OK: {data['n_entries']} ({frac:.1%} of expected {exp})"))

    # 2) reco presence
    reco = (data["omtfPt"] > 0).mean()
    if reco < 0.2:
        msgs.append(("WARN", f"low reco fraction (omtfPt>0): {reco:.1%}"))
    else:
        msgs.append(("PASS", f"reco fraction (omtfPt>0): {reco:.1%}"))

    # 3) eta acceptance
    in_acc = ((np.abs(data["muonPropEta"]) >= 0.82) & (np.abs(data["muonPropEta"]) <= 1.24)).mean()
    if in_acc < 0.6:
        msgs.append(("WARN", f"low OMTF eta acceptance: {in_acc:.1%}"))
    else:
        msgs.append(("PASS", f"OMTF eta acceptance: {in_acc:.1%}"))

    # 4) dxy expectations (skip strict checks for B4)
    if cfg["max_dxy"] is not None:
        dxy = data["muonDxy"]
        if cfg["max_dxy"] == 0:
            prompt = (np.abs(dxy) < 0.5).mean()
            if prompt < 0.8:
                msgs.append(("WARN", f"prompt fraction low (|dxy|<0.5): {prompt:.1%}"))
            else:
                msgs.append(("PASS", f"prompt fraction (|dxy|<0.5): {prompt:.1%}"))
        else:
            overflow = (np.abs(dxy) > cfg["max_dxy"] * 1.05).mean()
            if overflow > 0.02:
                msgs.append(("WARN", f"dxy overflow above {cfg['max_dxy']}cm: {overflow:.2%}"))
            else:
                msgs.append(("PASS", f"dxy overflow above {cfg['max_dxy']}cm: {overflow:.2%}"))

    # 5) charge consistency on reconstructed candidates
    m = data["omtfPt"] > 0
    if np.count_nonzero(m) > 0:
        charge_ok = (data["muonCharge_i"][m] == data["omtfCharge_i"][m]).mean()
        if charge_ok < 0.75:
            msgs.append(("WARN", f"charge agreement low: {charge_ok:.1%}"))
        else:
            msgs.append(("PASS", f"charge agreement: {charge_ok:.1%}"))

    # overall
    state = "PASS"
    if any(k == "FAIL" for k, _ in msgs):
        state = "FAIL"
    elif any(k == "WARN" for k, _ in msgs):
        state = "WARN"

    # 6) NanoAOD checks
    if nano is None:
        msgs.append(("FAIL", "NanoAOD files not found for this dataset"))
    else:
        nevt = max(1, int(nano["n_events"]))
        gen_ev = float((nano["nGenMuon"] > 0).mean())
        omtf_ev = float((nano["nomtf"] > 0).mean())
        msgs.append(("PASS", f"Nano events loaded: {nano['n_events']}"))
        msgs.append(("PASS", f"Nano events with >=1 GenMuon: {gen_ev:.1%}"))
        msgs.append(("PASS", f"Nano events with >=1 omtf cand: {omtf_ev:.1%}"))

    return state, msgs


def plot_dataset(ds, data, nano):
    cfg = DATASETS[ds]
    fig, axes = plt.subplots(3, 3, figsize=(15, 11))
    axes = axes.flatten()
    fig.suptitle(f"{ds}: {cfg['label']}")

    axes[0].hist(data["muonPt"], bins=60, color="tab:blue", alpha=0.8)
    axes[0].set_title("muonPt [GeV]")

    axes[1].hist(data["omtfPt"], bins=60, color="tab:green", alpha=0.8)
    axes[1].set_title("omtfPt [GeV]")

    axes[2].hist(data["muonPropEta"], bins=60, color="tab:orange", alpha=0.8)
    axes[2].axvline(0.82, color="r", ls="--", lw=1)
    axes[2].axvline(1.24, color="r", ls="--", lw=1)
    axes[2].axvline(-0.82, color="r", ls="--", lw=1)
    axes[2].axvline(-1.24, color="r", ls="--", lw=1)
    axes[2].set_title("muonPropEta")

    axes[3].hist(data["muonDxy"], bins=60, color="tab:red", alpha=0.8)
    axes[3].set_title("muonDxy [cm]")

    axes[4].hist(data["nHits"], bins=50, color="tab:purple", alpha=0.8)
    axes[4].set_title("number of hits per candidate")

    axes[5].hist(data["nFired"], bins=np.arange(-0.5, 18.5, 1.0), color="tab:brown", alpha=0.8)
    axes[5].set_title("fired layers")

    if nano is not None and nano["n_events"] > 0:
        axes[6].hist(nano["nGenMuon"], bins=np.arange(-0.5, 10.5, 1.0), color="tab:cyan", alpha=0.8)
        axes[6].set_title("Nano: nGenMuon per event")

        if nano["GenMuon_pt"].size > 0:
            axes[7].hist(nano["GenMuon_pt"], bins=60, color="tab:pink", alpha=0.8)
            axes[7].set_title("Nano: GenMuon_pt [GeV]")
        else:
            axes[7].text(0.5, 0.5, "No GenMuon entries", ha="center", va="center")
            axes[7].set_title("Nano: GenMuon_pt [GeV]")

        bins = np.arange(-0.5, 20.5, 1.0)
        axes[8].hist(nano["nMuonStubTps"], bins=bins, histtype="step", lw=1.6, label="nMuonStubTps")
        axes[8].hist(nano["nMuonStubKmtf"], bins=bins, histtype="step", lw=1.6, label="nMuonStubKmtf")
        axes[8].set_title("Nano: stub multiplicity per event")
        axes[8].legend(fontsize=8)
    else:
        axes[6].text(0.5, 0.5, "Nano files missing", ha="center", va="center")
        axes[6].set_title("Nano: nGenMuon per event")
        axes[7].text(0.5, 0.5, "Nano files missing", ha="center", va="center")
        axes[7].set_title("Nano: GenMuon_pt [GeV]")
        axes[8].text(0.5, 0.5, "Nano files missing", ha="center", va="center")
        axes[8].set_title("Nano: stub multiplicity per event")

    for ax in axes:
        ax.grid(alpha=0.3)

    fig.tight_layout()
    out = os.path.join(OUT_DIR, f"prod_check_{ds}.pdf")
    fig.savefig(out)
    plt.close(fig)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    report_lines = []
    report_lines.append("OMTF Production Dataset Quality Report")
    report_lines.append("=" * 70)
    report_lines.append(f"EOS base: {EOS_BASE}")
    report_lines.append("")

    summary = []

    for ds in DATASETS:
        files = find_files(ds)
        nano_files = find_nano_files(ds)
        if not files:
            report_lines.append(f"[{ds}] FAIL: no files found under {EOS_BASE}/{ds}")
            summary.append((ds, "FAIL", "no files found"))
            continue

        data = load_scalars(files)
        nano = load_nano(nano_files) if nano_files else None

        state, msgs = quality_checks(ds, data, nano)
        nano_info = f", {len(nano_files)} nano files" if nano_files else ", no nano files"
        summary.append((ds, state, f"{len(files)} hits files, {data['n_entries']} entries{nano_info}"))

        report_lines.append(f"[{ds}] {state} - {DATASETS[ds]['label']}")
        report_lines.append(f"  hits files: {len(files)}")
        report_lines.append(f"  hits entries: {data['n_entries']}")
        report_lines.append(f"  nano files: {len(nano_files)}")
        if nano is not None:
            report_lines.append(f"  nano events: {nano['n_events']}")
        for level, msg in msgs:
            report_lines.append(f"  - {level}: {msg}")
        report_lines.append("")

        plot_dataset(ds, data, nano)

    report_lines.append("\nSummary")
    report_lines.append("-" * 70)
    for ds, state, info in summary:
        report_lines.append(f"{ds}: {state} ({info})")

    out_report = os.path.join(OUT_DIR, "PROD_DATASET_QUALITY_REPORT.txt")
    with open(out_report, "w", encoding="utf-8") as f:
        f.write("\n".join(report_lines) + "\n")

    print(f"[OK] Report: {out_report}")
    print(f"[OK] Per-dataset plots: {OUT_DIR}/prod_check_<DS>.pdf")
    print("[OK] No mixed cross-dataset plots were produced.")


if __name__ == "__main__":
    main()
