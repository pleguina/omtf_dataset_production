#!/usr/bin/env python3
"""Truth unification audit for GenMuon/TpMuon/stub links.

Emits category counts:
- track_only
- tp_only_hard_scatter
- tp_only_pu
- dual_same_object
- dual_conflict
- dual_unresolved
- ambiguous
- noSelectedMuonLink

And residual summaries for TpMuon<->GenMuon matched pairs.
"""

import argparse
import json
from collections import defaultdict
from pathlib import Path

import numpy as np
import uproot


DUAL_SAME = 1
DUAL_CONFLICT = 2
DUAL_UNRES = 3


def ang_diff(a, b):
    d = a - b
    while d > np.pi:
        d -= 2 * np.pi
    while d < -np.pi:
        d += 2 * np.pi
    return d


def flatten(jag):
    if len(jag) == 0:
        return np.array([])
    return np.concatenate([np.asarray(x) for x in jag])


def summary(arr):
    if len(arr) == 0:
        return {"n": 0}
    return {
        "n": int(len(arr)),
        "mean": float(np.mean(arr)),
        "p50": float(np.quantile(arr, 0.5)),
        "p95": float(np.quantile(arr, 0.95)),
        "max_abs": float(np.max(np.abs(arr))),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hits", required=True)
    ap.add_argument("--nano", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    f_hits = uproot.open(args.hits)
    f_nano = uproot.open(args.nano)

    t = f_hits["simOmtfPhase2Digis/OMTFAllInputTree"]
    n = f_nano["Events"]

    keys = ["reg_runNum", "reg_lumiNum", "reg_eventNum64"]
    hkey = list(zip(*(t[k].array(library="np").tolist() for k in keys)))
    nkey = list(zip(
        n["run"].array(library="np").tolist(),
        n["luminosityBlock"].array(library="np").tolist(),
        n["event"].array(library="np").tolist(),
    ))
    npos = {k: i for i, k in enumerate(nkey)}

    counters = defaultdict(int)

    tr = t["reg_stub_trackId"].array(library="np")
    tp = t["reg_stub_tpId"].array(library="np")
    amb = t["reg_stub_ambiguous"].array(library="np")
    dual = t["reg_stub_dualIdRelation"].array(library="np") if "reg_stub_dualIdRelation" in t.keys() else None

    for i in range(len(tr)):
        for j in range(len(tr[i])):
            tid = int(tr[i][j])
            tpid = int(tp[i][j])
            ambiguous = int(amb[i][j]) > 0
            if ambiguous:
                counters["ambiguous"] += 1

            if tid > 0 and tpid == 0:
                counters["track_only"] += 1
            elif tid == 0 and tpid > 0:
                # Determine PU/hard-scatter from TpMuon_eventId if available.
                if "TpMuon_eventId" in t.keys() and 1 <= tpid <= len(t["TpMuon_eventId"].array(library="np")[i]):
                    evt_id = int(t["TpMuon_eventId"].array(library="np")[i][tpid - 1])
                    if evt_id == 0:
                        counters["tp_only_hard_scatter"] += 1
                    else:
                        counters["tp_only_pu"] += 1
                else:
                    counters["tp_only_hard_scatter"] += 1
            elif tid > 0 and tpid > 0:
                rel = int(dual[i][j]) if dual is not None else 0
                if rel == DUAL_SAME:
                    counters["dual_same_object"] += 1
                elif rel == DUAL_CONFLICT:
                    counters["dual_conflict"] += 1
                elif rel == DUAL_UNRES:
                    counters["dual_unresolved"] += 1
                else:
                    counters["dual_unresolved"] += 1
            else:
                counters["noSelectedMuonLink"] += 1

    # Residuals for TpMuon<->GenMuon exact links.
    res_charge = []
    res_pt = []
    res_eta = []
    res_phi = []
    res_vx = []
    res_vy = []
    res_dxyh = []

    tp_pt = t["TpMuon_pt"].array(library="np") if "TpMuon_pt" in t.keys() else []
    tp_eta = t["TpMuon_eta"].array(library="np") if "TpMuon_eta" in t.keys() else []
    tp_phi = t["TpMuon_phi"].array(library="np") if "TpMuon_phi" in t.keys() else []
    tp_q = t["TpMuon_charge"].array(library="np") if "TpMuon_charge" in t.keys() else []
    tp_vx = t["TpMuon_vx"].array(library="np") if "TpMuon_vx" in t.keys() else []
    tp_vy = t["TpMuon_vy"].array(library="np") if "TpMuon_vy" in t.keys() else []
    tp_dxyh = t["TpMuon_dxyHelix"].array(library="np") if "TpMuon_dxyHelix" in t.keys() else []
    tp_gidx = t["TpMuon_genMuonIdx"].array(library="np") if "TpMuon_genMuonIdx" in t.keys() else []

    # Collapse duplicated regions: evaluate each event key once.
    first_row_for_key = {}
    for i, k in enumerate(hkey):
        if k not in first_row_for_key:
            first_row_for_key[k] = i

    # Nano jagged arrays.
    g_pt = n["GenMuon_pt"].array(library="np") if "GenMuon_pt" in n.keys() else []
    g_eta = n["GenMuon_eta"].array(library="np") if "GenMuon_eta" in n.keys() else []
    g_phi = n["GenMuon_phi"].array(library="np") if "GenMuon_phi" in n.keys() else []
    g_q = n["GenMuon_charge"].array(library="np") if "GenMuon_charge" in n.keys() else []
    g_vx = n["GenMuon_vx"].array(library="np") if "GenMuon_vx" in n.keys() else []
    g_vy = n["GenMuon_vy"].array(library="np") if "GenMuon_vy" in n.keys() else []
    g_dxy = n["GenMuon_dXY"].array(library="np") if "GenMuon_dXY" in n.keys() else []

    for k, i in first_row_for_key.items():
        ni = npos.get(k)
        if ni is None:
            continue
        if i >= len(tp_gidx):
            continue

        for j, gidx in enumerate(tp_gidx[i]):
            gi = int(gidx)
            if gi < 0:
                continue
            if gi >= len(g_pt[ni]):
                continue
            res_charge.append(float(tp_q[i][j] - g_q[ni][gi]))
            res_pt.append(float(tp_pt[i][j] - g_pt[ni][gi]))
            res_eta.append(float(tp_eta[i][j] - g_eta[ni][gi]))
            res_phi.append(float(ang_diff(float(tp_phi[i][j]), float(g_phi[ni][gi]))))
            res_vx.append(float(tp_vx[i][j] - g_vx[ni][gi]))
            res_vy.append(float(tp_vy[i][j] - g_vy[ni][gi]))
            res_dxyh.append(float(abs(tp_dxyh[i][j]) - abs(g_dxy[ni][gi])))

    out = {
        "status": "pass" if counters["dual_conflict"] == 0 else "fail",
        "categories": {
            "track_only": int(counters["track_only"]),
            "tp_only_hard_scatter": int(counters["tp_only_hard_scatter"]),
            "tp_only_pu": int(counters["tp_only_pu"]),
            "dual_same_object": int(counters["dual_same_object"]),
            "dual_conflict": int(counters["dual_conflict"]),
            "dual_unresolved": int(counters["dual_unresolved"]),
            "ambiguous": int(counters["ambiguous"]),
            "noSelectedMuonLink": int(counters["noSelectedMuonLink"]),
        },
        "residuals": {
            "charge": summary(np.asarray(res_charge)),
            "pT": summary(np.asarray(res_pt)),
            "eta": summary(np.asarray(res_eta)),
            "phi": summary(np.asarray(res_phi)),
            "vx": summary(np.asarray(res_vx)),
            "vy": summary(np.asarray(res_vy)),
            "dxyHelix": summary(np.asarray(res_dxyh)),
        },
    }

    Path(args.out).write_text(json.dumps(out, indent=2) + "\n")
    print(f"Wrote {args.out} status={out['status']}")
    raise SystemExit(0 if out["status"] == "pass" else 2)


if __name__ == "__main__":
    main()
