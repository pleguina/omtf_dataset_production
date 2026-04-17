#!/usr/bin/env python3
"""
make_graphs.py — Convert OMTF OMTFAllInputTree ntuples to PyG Data objects
for HECIN+OC training.

Source tree: simOmtfPhase2Digis/OMTFAllInputTree
Each entry = one (event, OMTF processor region) pair, containing ALL stubs in
that region (not just candidate-matched stubs).  This is the correct input for
Object Condensation training, which needs the full per-window stub set with
per-stub track-ID labels.

Output schema (torch_geometric.data.Data per region):
  x_cont         : FloatTensor [N, 6]   continuous node features
                   [phi_norm, phiB_norm, eta, qual_norm, r_norm, bx_norm]
  node_type      : LongTensor  [N]      detector type: 0=DT, 1=CSC, 2=RPC
  layer_id       : LongTensor  [N]      logic layer 0-17
  edge_index     : LongTensor  [2, E]   bidirectional edges
  edge_attr_cont : FloatTensor [E, 4]   [delta_phi, abs_delta_r, delta_r_sq,
                                         kappa_hat (= 2*delta_phi / delta_r_sq)]
  edge_type      : LongTensor  [E]      0=cross-layer, 1=intra-station phiB pair
  track_id       : LongTensor  [N]      0=noise/PU, 1..K=gen-muon index
  is_ambiguous   : BoolTensor  [N]      True if SimTrack truth is unreliable
  edge_y         : FloatTensor [E]      1.0 same track, 0.0 different, -1.0 ambiguous
  n_true_tracks  : int                  number of distinct non-noise track IDs
  has_pu         : bool                 True for B* (pileup) samples
  sample_id      : str                  S1/S2/.../B4
  event_num      : int
  i_processor    : int

Note: OMTFHitsTree (per-candidate, per-event) is NOT used here.  It is retained
in the same ROOT files for trigger efficiency / fake-rate studies and can be read
independently.

Note on NanoAOD (omtf_nano_*.root):
  Provides complementary event-level information:
    - GenMuon_{pt,eta,phi,charge,dXY,lXY,etaSt1,etaSt2,phiSt1,phiSt2,pdgId,status}
    - omtf_{hwPt,hwEta,hwPhi,hwDXY,hwQual,processor,muIdx}
    - MuonStubTps_*, MuonStubKmtf_* (trigger-primitive stubs, coarser than AllInputTree)
  The NanoAOD does NOT contain per-stub SimTrack truth or phiBHw, so it cannot
  replace OMTFAllInputTree for GNN training.  It is useful for trigger efficiency
  curves (matching omtf_hw* to GenMuon_*) and offline cross-checks.

EOS data path:
  /eos/user/p/pleguina/omtf_hecin_datasets/prod/{S1,S2,S3,S4,S5,B1,B2,B3,B4}/
      omtf_hits_<DATASET>_<N>.root   <- contains OMTFAllInputTree + OMTFHitsTree
      omtf_nano_<DATASET>_<N>.root   <- NanoAOD complement

Dataset sizes (all files already generated):
  S1=2000, S2=2000, S3=2000 files; S4=600, S5=600 files (500 events/job)
  B1=800,  B2=800,  B3=800  files; B4=798 files

Usage:
  python3 scripts/make_graphs.py \\
      --input root://eosuser.cern.ch//eos/user/p/pleguina/omtf_hecin_datasets/prod/S1/omtf_hits_S1_0.root \\
      --output graphs/S1_0.pt --sample-id S1

  # Directory mode: convert every omtf_hits_*.root in a local/xrootd folder
  python3 scripts/make_graphs.py \\
      --input /eos/user/p/pleguina/omtf_hecin_datasets/prod/S1/ \\
      --outdir graphs/S1/ --sample-id S1
"""

import argparse
import sys
from pathlib import Path

try:
    import uproot
    import numpy as np
    import torch
    from torch_geometric.data import Data
    HAS_DEPS = True
except ImportError as exc:
    HAS_DEPS = False
    _MISSING = str(exc)

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

TREE = "simOmtfPhase2Digis/OMTFAllInputTree"

# Logic layer metadata: layer_id -> (name, det_type, is_phiB_bending)
#   det_type: 0=DT barrel, 1=CSC endcap, 2=RPC
LAYER_INFO = {
    0:  ("MB1_phi",  0, False),
    1:  ("MB1_phiB", 0, True),
    2:  ("MB2_phi",  0, False),
    3:  ("MB2_phiB", 0, True),
    4:  ("MB3_phi",  0, False),
    5:  ("MB3_phiB", 0, True),
    6:  ("ME1/3",    1, False),
    7:  ("ME2/2",    1, False),
    8:  ("ME3/2",    1, False),
    9:  ("ME1/2",    1, False),
    10: ("RB1in",    2, False),
    11: ("RB1out",   2, False),
    12: ("RB2in",    2, False),
    13: ("RB2out",   2, False),
    14: ("RB3",      2, False),
    15: ("RE1/3",    2, False),
    16: ("RE2/3",    2, False),
    17: ("RE3/3",    2, False),
}

# DT intra-station phiB pairs (phi_layer, phiB_layer)
# Each DT station contributes one phi stub (even layer) and one phiB stub (odd layer).
# These pairs are local bending-angle measurements — edges within the same station.
DT_INTRA_PAIRS = [(0, 1), (2, 3), (4, 5)]

# Approximate radial position per logic layer [cm] (used as fallback when r=0)
LAYER_R_FALLBACK = {
    0: 231.5, 1: 231.5,
    2: 309.5, 3: 309.5,
    4: 404.5, 5: 404.5,
    6: 525.0, 7: 600.0, 8: 710.0, 9: 490.0,
    10: 200.0, 11: 210.0, 12: 270.0, 13: 280.0,
    14: 380.0, 15: 490.0, 16: 540.0, 17: 600.0,
}
R_NORM = 750.0  # normalisation [cm]

# phi hardware scale: 5400 bins over 2*pi
PHI_HW_SCALE = 2.0 * np.pi / 5400.0
# phiBHw normalisation (typical half-range ~512 HW units)
PHIB_HW_NORM = 512.0

BACKGROUND_SAMPLES = frozenset({"B1", "B2", "B3", "B4"})


# ---------------------------------------------------------------------------
# Per-region graph builder
# ---------------------------------------------------------------------------

def region_to_data(
    layers:    np.ndarray,  # [N] int   logic layer 0-17
    phi_hw:    np.ndarray,  # [N] int   absolute phi HW
    phiB_hw:   np.ndarray,  # [N] int   DT bending angle HW (0 for non-DT)
    eta_hw:    np.ndarray,  # [N] int   eta HW
    r_cm:      np.ndarray,  # [N] int   radial distance cm (0 if not available)
    quality:   np.ndarray,  # [N] int   quality HW 0-15
    bx:        np.ndarray,  # [N] int   BX offset (-3..+3 typically)
    track_id:  np.ndarray,  # [N] int   0=noise/PU, 1..K=gen-muon index
    ambiguous: np.ndarray,  # [N] bool  1=ambiguous SimTrack truth
    sample_id: str,
    event_num: int,
    i_processor: int,
) -> "Data | None":
    n = int(len(layers))
    if n == 0:
        return None

    layers    = np.asarray(layers,    dtype=np.int32)
    phi_hw    = np.asarray(phi_hw,    dtype=np.float32)
    phiB_hw   = np.asarray(phiB_hw,  dtype=np.float32)
    eta_hw    = np.asarray(eta_hw,    dtype=np.float32)
    quality   = np.asarray(quality,   dtype=np.float32)
    bx        = np.asarray(bx,        dtype=np.float32)
    track_id  = np.asarray(track_id,  dtype=np.int64)
    ambiguous = np.asarray(ambiguous, dtype=bool)

    # Radial position: use branch value when non-zero, else layer lookup
    r = np.where(
        r_cm != 0,
        np.asarray(r_cm, dtype=np.float32),
        np.array([LAYER_R_FALLBACK.get(int(l), 300.0) for l in layers], dtype=np.float32),
    )

    # Detector type per node
    det_type = np.array(
        [LAYER_INFO.get(int(l), ("?", 2, False))[1] for l in layers],
        dtype=np.int64,
    )

    # ---- Continuous node features [N, 6] ----
    phi_norm  = phi_hw  * PHI_HW_SCALE / np.pi   # ~[-1, 1]
    phiB_norm = phiB_hw / PHIB_HW_NORM            # bending angle normalised
    eta_phys  = eta_hw  * 0.010875                 # HW -> physical eta
    qual_norm = quality / 15.0
    r_norm    = r / R_NORM
    bx_norm   = bx / 3.0                           # typical range [-1, 1]

    x_cont = np.stack([phi_norm, phiB_norm, eta_phys, qual_norm, r_norm, bx_norm], axis=1)

    # ---- Edge construction ----
    # G1: cross-layer bidirectional pairs (different logic layers)
    src_cross, dst_cross = [], []
    for i in range(n):
        for j in range(i + 1, n):
            if layers[i] != layers[j]:
                src_cross.append(i); dst_cross.append(j)
                src_cross.append(j); dst_cross.append(i)

    # G5 extension: intra-station phi <-> phiB pairs within each DT station
    src_intra, dst_intra = [], []
    for phi_layer, phiB_layer in DT_INTRA_PAIRS:
        idx_phi  = np.where(layers == phi_layer)[0]
        idx_phiB = np.where(layers == phiB_layer)[0]
        for ip in idx_phi:
            for ib in idx_phiB:
                src_intra.append(int(ip)); dst_intra.append(int(ib))
                src_intra.append(int(ib)); dst_intra.append(int(ip))

    n_cross = len(src_cross)
    n_intra = len(src_intra)

    src_all = np.array(src_cross + src_intra, dtype=np.int64)
    dst_all = np.array(dst_cross + dst_intra, dtype=np.int64)
    edge_type_arr = np.array([0] * n_cross + [1] * n_intra, dtype=np.int64)

    E = int(len(src_all))
    if E > 0:
        edge_index = np.stack([src_all, dst_all], axis=0)

        dphi = (phi_hw[src_all] - phi_hw[dst_all]) * PHI_HW_SCALE
        dr   = r[src_all] - r[dst_all]
        dr2  = r[src_all] ** 2 - r[dst_all] ** 2
        with np.errstate(divide="ignore", invalid="ignore"):
            kappa = np.where(np.abs(dr2) > 1.0, 2.0 * dphi / dr2, 0.0)

        edge_attr = np.stack([dphi, np.abs(dr), dr2, kappa], axis=1).astype(np.float32)
    else:
        edge_index    = np.empty((2, 0), dtype=np.int64)
        edge_attr     = np.empty((0, 4), dtype=np.float32)
        edge_type_arr = np.empty((0,),   dtype=np.int64)

    # ---- Per-edge truth ----
    # 1.0 = both endpoints belong to same non-noise track
    # 0.0 = different tracks or at least one is noise
    # -1.0 = at least one endpoint has ambiguous SimTrack assignment
    if E > 0:
        ti = track_id[src_all]
        tj = track_id[dst_all]
        ai = ambiguous[src_all]
        aj = ambiguous[dst_all]
        edge_y = np.where(
            ai | aj,
            np.float32(-1.0),
            np.where((ti > 0) & (ti == tj), np.float32(1.0), np.float32(0.0)),
        ).astype(np.float32)
    else:
        edge_y = np.empty((0,), dtype=np.float32)

    n_true_tracks = int(np.unique(track_id[track_id > 0]).size)

    return Data(
        x_cont         = torch.from_numpy(x_cont),
        node_type      = torch.from_numpy(det_type),
        layer_id       = torch.from_numpy(layers.astype(np.int64)),
        edge_index     = torch.from_numpy(edge_index),
        edge_attr_cont = torch.from_numpy(edge_attr),
        edge_type      = torch.from_numpy(edge_type_arr),
        track_id       = torch.from_numpy(track_id),
        is_ambiguous   = torch.from_numpy(ambiguous),
        edge_y         = torch.from_numpy(edge_y),
        n_true_tracks  = n_true_tracks,
        has_pu         = (sample_id in BACKGROUND_SAMPLES),
        sample_id      = sample_id,
        event_num      = int(event_num),
        i_processor    = int(i_processor),
        num_nodes      = n,
    )


# ---------------------------------------------------------------------------
# File reader
# ---------------------------------------------------------------------------

def make_graphs_from_file(
    root_path:   Path,
    sample_id:   str,
    max_regions: int = -1,
) -> list:
    """Read OMTFAllInputTree from a ROOT file and return a list of Data objects."""
    graphs = []

    path_str = str(root_path)
    with uproot.open(path_str) as f:
        if TREE not in f:
            bare = TREE.split("/")[-1]
            alt  = next((k for k in f.keys() if bare in str(k)), None)
            if alt:
                tree = f[alt]
                print(f"  [WARN] Using key '{alt}' instead of '{TREE}'")
            else:
                print(f"  [ERROR] '{TREE}' not found. Available: {list(f.keys())[:10]}")
                return graphs
        else:
            tree = f[TREE]

        total = tree.num_entries
        n_entries = min(total, max_regions) if max_regions > 0 else total
        print(f"  {n_entries}/{total} regions from {Path(path_str).name}")

        keys = set(tree.keys())

        def read(name):
            return tree[name].array(library="np", entry_stop=n_entries) if name in keys else None

        event_nums   = read("reg_eventNum")
        i_processors = read("reg_iProcessor")

        layers_arr  = read("reg_stub_layer")
        phi_arr     = read("reg_stub_phiHw")
        phiB_arr    = read("reg_stub_phiBHw")
        eta_arr     = read("reg_stub_etaHw")
        r_arr       = read("reg_stub_r")
        qual_arr    = read("reg_stub_quality")
        bx_arr      = read("reg_stub_bx")
        trackid_arr = read("reg_stub_trackId")
        ambig_arr   = read("reg_stub_ambiguous")

        if layers_arr is None:
            print("  [ERROR] 'reg_stub_layer' missing — is this OMTFAllInputTree?")
            return graphs

        def zeros_jagged(ref, dtype):
            return np.array([np.zeros(len(ref[i]), dtype=dtype)
                             for i in range(len(ref))], dtype=object)

        if phiB_arr    is None: phiB_arr    = zeros_jagged(layers_arr, np.int16)
        if r_arr       is None: r_arr       = zeros_jagged(layers_arr, np.int16)
        if bx_arr      is None: bx_arr      = zeros_jagged(layers_arr, np.int8)
        if trackid_arr is None: trackid_arr = zeros_jagged(layers_arr, np.int8)
        if ambig_arr   is None: ambig_arr   = zeros_jagged(layers_arr, np.uint8)

        for i in range(n_entries):
            d = region_to_data(
                layers    = np.asarray(layers_arr[i]),
                phi_hw    = np.asarray(phi_arr[i])     if phi_arr  is not None else np.zeros(len(layers_arr[i]), np.int16),
                phiB_hw   = np.asarray(phiB_arr[i]),
                eta_hw    = np.asarray(eta_arr[i])     if eta_arr  is not None else np.zeros(len(layers_arr[i]), np.int8),
                r_cm      = np.asarray(r_arr[i]),
                quality   = np.asarray(qual_arr[i])    if qual_arr is not None else np.zeros(len(layers_arr[i]), np.int8),
                bx        = np.asarray(bx_arr[i]),
                track_id  = np.asarray(trackid_arr[i]),
                ambiguous = np.asarray(ambig_arr[i]),
                sample_id   = sample_id,
                event_num   = int(event_nums[i])    if event_nums   is not None else 0,
                i_processor = int(i_processors[i])  if i_processors is not None else 0,
            )
            if d is not None:
                graphs.append(d)

    return graphs


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Build PyG Data objects from OMTF OMTFAllInputTree ntuples"
    )
    parser.add_argument("--input",       required=True,
                        help="Single .root file or directory of omtf_hits_*.root files")
    parser.add_argument("--output",      default=None,
                        help="Output .pt file (single-file mode)")
    parser.add_argument("--outdir",      default=None,
                        help="Output directory (directory mode)")
    parser.add_argument("--sample-id",   default="UNKNOWN", dest="sample_id",
                        help="Dataset label, e.g. S1, B2")
    parser.add_argument("--max-regions", type=int, default=-1, dest="max_regions",
                        help="Limit regions per file (default: all)")
    args = parser.parse_args()

    if not HAS_DEPS:
        print(f"[ERROR] Missing dependency: {_MISSING}")
        print("Install: pip install uproot numpy torch torch_geometric")
        sys.exit(1)

    inp = Path(args.input)

    if inp.is_file() or str(inp).startswith("root://"):
        root_files = [(inp, args.sample_id, args.output)]
    elif inp.is_dir():
        found = sorted(inp.rglob("omtf_hits_*.root"))
        if not found:
            print(f"[ERROR] No omtf_hits_*.root found under {inp}")
            sys.exit(1)
        outdir = Path(args.outdir or inp)
        outdir.mkdir(parents=True, exist_ok=True)
        root_files = [
            (rf, args.sample_id,
             str(outdir / rf.name.replace("omtf_hits_", "graphs_").replace(".root", ".pt")))
            for rf in found
        ]
    else:
        print(f"[ERROR] {inp} is not a file or directory")
        sys.exit(1)

    total_graphs = 0
    for rfile, sid, out_path in root_files:
        print(f"\n[{sid}] {rfile}")
        graphs = make_graphs_from_file(rfile, sid, max_regions=args.max_regions)
        if not graphs:
            print("  [WARN] No graphs produced.")
            continue

        if out_path:
            torch.save(graphs, out_path)
            print(f"  Saved {len(graphs)} graphs -> {out_path}")

        n_nodes  = [g.num_nodes             for g in graphs]
        n_edges  = [g.edge_index.shape[1]   for g in graphs]
        n_sig    = [int((g.track_id > 0).sum().item()) for g in graphs]
        n_tracks = [g.n_true_tracks          for g in graphs]
        print(f"  Nodes/region  : {np.mean(n_nodes):.1f} +/- {np.std(n_nodes):.1f}")
        print(f"  Edges/region  : {np.mean(n_edges):.1f} +/- {np.std(n_edges):.1f}")
        print(f"  Signal stubs  : {np.mean(n_sig):.1f} avg")
        print(f"  Tracks/region : {np.mean(n_tracks):.2f} avg")
        total_graphs += len(graphs)

    print(f"\nTotal graphs: {total_graphs}")


if __name__ == "__main__":
    main()
