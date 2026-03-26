#!/usr/bin/env python3
"""
make_graphs.py — Convert OMTF DataROOTDumper2 ntuples to PyG Data objects.

DataROOTDumper2 format (simOmtfPhase2Digis/OMTFHitsTree):
  - ONE entry per OMTF candidate matched to a gen muon
  - All muon/OMTF variables are scalars per entry
  - `hits` is std::vector<uint64_t> where each uint64 packs:
       bits  0– 7: layer   (int8,  logic layer 0–17)
       bits  8–15: quality (int8,  qualityHw)
       bits 16–23: etaHw   (int8,  eta = etaHw × 0.010875)
       bits 24–31: valid   (int8)
       bits 32–47: deltaR  (int16)
       bits 48–63: phiDist (int16, φ displacement from reference stub)

Each candidate → one torch_geometric.data.Data with:
  x              : FloatTensor [N, 6]   node features (stubs)
  node_type      : LongTensor  [N]      detector type (0=DT, 1=CSC, 2=RPC)
  layer_id       : LongTensor  [N]      logic layer 0–17
  edge_index     : LongTensor  [2, E]   all cross-layer pairs (bidirectional)
  edge_attr      : FloatTensor [E, 3]   [Δlayer/17, Δ(phiDist/4096), Δeta]
  gen_pt         : FloatTensor [1]      gen muon pT
  gen_dxy        : FloatTensor [1]      gen muon dxy [cm]
  gen_rho        : FloatTensor [1]      gen muon transverse decay radius [cm]
  y              : LongTensor  [1]      1 if |dxy| > 1 cm (displaced), else 0
  sample_id      : str                  dataset label (S1, S2, …)
  weight         : float                1/gen_pt for LLP samples, else 1.0

x columns:
  0  layer_norm  : layer / 17.0
  1  phi_norm    : phiDist / 4096.0
  2  eta         : etaHw × 0.010875
  3  qual_norm   : quality / 15.0
  4  valid       : valid > 0
  5  is_bending  : (layer % 2 == 1) and (layer < 6)  [DT bending layers]

Usage:
  python3 scripts/make_graphs.py --input test/smoke/S1/omtf_hits.root \\
      --output test/graphs/S1_graphs.pt --sample-id S1

  python3 scripts/make_graphs.py --input test/smoke/ --outdir test/graphs/
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
# OMTF 18-layer logic-layer map
# ---------------------------------------------------------------------------
LAYER_INFO = {
    0:  ("MB1",    0, False),   # DT MB1 phi
    1:  ("MB1b",   0, True),    # DT MB1 phiB (bending)
    2:  ("MB2",    0, False),
    3:  ("MB2b",   0, True),
    4:  ("MB3",    0, False),
    5:  ("MB3b",   0, True),
    6:  ("ME1/3",  1, False),   # CSC
    7:  ("ME2/2",  1, False),
    8:  ("ME3/2",  1, False),
    9:  ("ME1/2",  1, False),
    10: ("RB1in",  2, False),   # RPC barrel
    11: ("RB1out", 2, False),
    12: ("RB2in",  2, False),
    13: ("RB2out", 2, False),
    14: ("RB3",    2, False),
    15: ("RE1/3",  2, False),   # RPC endcap
    16: ("RE2/3",  2, False),
    17: ("RE3/3",  2, False),
}

LLP_SAMPLE_IDS = {"S2", "S5", "D1"}

DISPLACED_DXY_CM = 1.0   # threshold for binary displaced label


# ---------------------------------------------------------------------------
# Hit decoder
# ---------------------------------------------------------------------------
def decode_hits(raw_hits) -> tuple[np.ndarray, ...]:
    """Unpack list of uint64 hits into component arrays.

    Returns:
        layers   [N] int32    logic layer 0-17
        quality  [N] float32  qualityHw
        eta      [N] float32  etaHw * 0.010875 (physical eta)
        phi_dist [N] float32  phiDist hardware units (signed int16)
        valid    [N] float32  1 if valid, else 0
        delta_r  [N] float32  deltaR hardware units (signed int16)
    """
    n = len(raw_hits)
    layers   = np.empty(n, dtype=np.int32)
    quality  = np.empty(n, dtype=np.float32)
    eta      = np.empty(n, dtype=np.float32)
    phi_dist = np.empty(n, dtype=np.float32)
    valid    = np.empty(n, dtype=np.float32)
    delta_r  = np.empty(n, dtype=np.float32)

    for k, h in enumerate(raw_hits):
        h = int(h)
        layer_k  = int(np.array(h & 0xFF,             dtype=np.uint8).view(np.int8))
        qual_k   = int(np.array((h >> 8)  & 0xFF,     dtype=np.uint8).view(np.int8))
        etaHw    = int(np.array((h >> 16) & 0xFF,     dtype=np.uint8).view(np.int8))
        valid_k  = int((h >> 24) & 0xFF)
        dr_k     = int(np.array((h >> 32) & 0xFFFF,   dtype=np.uint16).view(np.int16))
        phi_k    = int(np.array((h >> 48) & 0xFFFF,   dtype=np.uint16).view(np.int16))

        layers[k]   = layer_k
        quality[k]  = float(qual_k)
        eta[k]      = float(etaHw) * 0.010875
        phi_dist[k] = float(phi_k)
        valid[k]    = float(valid_k > 0)
        delta_r[k]  = float(dr_k)

    return layers, quality, eta, phi_dist, valid, delta_r


# ---------------------------------------------------------------------------
# Graph builder
# ---------------------------------------------------------------------------
def candidate_to_data(
    layers:   np.ndarray,
    quality:  np.ndarray,
    eta:      np.ndarray,
    phi_dist: np.ndarray,
    valid:    np.ndarray,
    muon_pt:  float,
    muon_dxy: float,
    muon_rho: float,
    sample_id: str,
) -> "Data | None":
    n = len(layers)
    if n == 0:
        return None

    # Derived node properties
    is_bending = np.array(
        [(int(l) % 2 == 1 and int(l) < 6) for l in layers], dtype=np.float32
    )
    det_type = np.array(
        [0 if l < 6 else (1 if l < 10 else 2) for l in layers], dtype=np.int64
    )

    # Node features [N, 6]
    x = np.stack([
        layers.astype(np.float32)   / 17.0,
        phi_dist.astype(np.float32) / 4096.0,
        eta.astype(np.float32),
        quality.astype(np.float32)  / 15.0,
        valid.astype(np.float32),
        is_bending,
    ], axis=1)

    # Cross-layer bidirectional edges (all pairs on different layers)
    src_list, dst_list = [], []
    for i in range(n):
        for j in range(i + 1, n):
            if layers[i] != layers[j]:
                src_list.append(i); dst_list.append(j)
                src_list.append(j); dst_list.append(i)

    E = len(src_list)
    if E > 0:
        src = np.array(src_list, dtype=np.int64)
        dst = np.array(dst_list, dtype=np.int64)
        edge_index = np.stack([src, dst], axis=0)
        edge_attr  = np.stack([
            (layers[src]   - layers[dst]).astype(np.float32)   / 17.0,
            (phi_dist[src] - phi_dist[dst]).astype(np.float32) / 4096.0,
            (eta[src]      - eta[dst]).astype(np.float32),
        ], axis=1)
    else:
        edge_index = np.empty((2, 0), dtype=np.int64)
        edge_attr  = np.empty((0, 3),  dtype=np.float32)

    weight = 1.0
    if sample_id in LLP_SAMPLE_IDS and muon_pt > 0:
        weight = 1.0 / max(muon_pt, 1.0)

    is_displaced = int(abs(muon_dxy) > DISPLACED_DXY_CM)

    return Data(
        x          = torch.from_numpy(x),
        node_type  = torch.from_numpy(det_type),
        layer_id   = torch.from_numpy(layers.astype(np.int64)),
        edge_index = torch.from_numpy(edge_index),
        edge_attr  = torch.from_numpy(edge_attr),
        gen_pt     = torch.tensor([muon_pt],  dtype=torch.float32),
        gen_dxy    = torch.tensor([muon_dxy], dtype=torch.float32),
        gen_rho    = torch.tensor([muon_rho], dtype=torch.float32),
        y          = torch.tensor([is_displaced], dtype=torch.long),
        sample_id  = sample_id,
        weight     = weight,
        num_nodes  = n,
    )


# ---------------------------------------------------------------------------
# File reader
# ---------------------------------------------------------------------------
def make_graphs_from_file(
    root_path:  Path,
    sample_id:  str,
    max_events: int = -1,
) -> list:
    TREE = "simOmtfPhase2Digis/OMTFHitsTree"
    graphs = []

    with uproot.open(str(root_path)) as f:
        if TREE not in f:
            print(f"[ERROR] Tree '{TREE}' not in {root_path}")
            return graphs

        tree = f[TREE]
        n_entries = min(tree.num_entries, max_events) if max_events > 0 else tree.num_entries
        print(f"  Processing {n_entries} candidates from {root_path.name}...")

        def read(name):
            if name in tree.keys():
                return tree[name].array(library="np", entry_stop=n_entries)
            return None

        hits_arr  = read("hits")
        pt_arr    = read("muonPt")
        dxy_arr   = read("muonDxy")
        rho_arr   = read("muonRho")

        if hits_arr is None:
            print("[ERROR] 'hits' branch missing — wrong file format?")
            return graphs

        for i in range(n_entries):
            raw = hits_arr[i]
            if len(raw) == 0:
                # no stubs for this candidate — skip (happens for very displaced muons)
                continue

            layers, quality, eta, phi_dist, valid, _ = decode_hits(raw)

            muon_pt  = float(pt_arr[i])  if pt_arr  is not None else 0.0
            muon_dxy = float(dxy_arr[i]) if dxy_arr is not None else 0.0
            muon_rho = float(rho_arr[i]) if rho_arr is not None else 0.0

            d = candidate_to_data(
                layers=layers, quality=quality, eta=eta, phi_dist=phi_dist,
                valid=valid, muon_pt=muon_pt, muon_dxy=muon_dxy,
                muon_rho=muon_rho, sample_id=sample_id,
            )
            if d is not None:
                graphs.append(d)

    return graphs


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="Build PyG graphs from OMTF DataROOTDumper2 ntuples")
    parser.add_argument("--input",      required=True, help="Single .root file or smoke-test directory")
    parser.add_argument("--output",     default=None,  help="Output .pt (single-file mode)")
    parser.add_argument("--outdir",     default=None,  help="Output directory (directory mode)")
    parser.add_argument("--sample-id",  default="UNKNOWN", dest="sample_id", help="Dataset label (S1, B1, …)")
    parser.add_argument("--max-events", type=int, default=-1, dest="max_events")
    args = parser.parse_args()

    if not HAS_DEPS:
        print(f"[ERROR] Missing dependency: {_MISSING}")
        print("Install with: pip install uproot numpy torch torch_geometric")
        sys.exit(1)

    inp = Path(args.input)

    if inp.is_file():
        root_files = [(inp, args.sample_id, args.output)]
    elif inp.is_dir():
        found = sorted(inp.rglob("omtf_hits.root"))
        if not found:
            print(f"[ERROR] No omtf_hits.root found under {inp}")
            sys.exit(1)
        outdir = Path(args.outdir) if args.outdir else inp
        outdir.mkdir(parents=True, exist_ok=True)
        root_files = [
            (rf, rf.parent.name if rf.name == "omtf_hits.root" else rf.stem,
             str(outdir / f"{rf.parent.name if rf.name == 'omtf_hits.root' else rf.stem}_graphs.pt"))
            for rf in found
        ]
    else:
        print(f"[ERROR] {inp} is not a file or directory")
        sys.exit(1)

    import numpy as np

    total = 0
    for rfile, sid, out_path in root_files:
        print(f"\n[{sid}] {rfile}")
        graphs = make_graphs_from_file(rfile, sid, max_events=args.max_events)
        if not graphs:
            print(f"  [WARN] No graphs produced (all hits empty? Very displaced sample?)")
            continue

        if out_path:
            torch.save(graphs, out_path)
            print(f"  Saved {len(graphs)} graphs → {out_path}")

        n_nodes  = [g.num_nodes              for g in graphs]
        n_edges  = [g.edge_index.shape[1]    for g in graphs]
        n_displ  = sum(int(g.y.item()) for g in graphs)
        print(f"  Nodes/graph  : {np.mean(n_nodes):.1f} ± {np.std(n_nodes):.1f}")
        print(f"  Edges/graph  : {np.mean(n_edges):.1f} ± {np.std(n_edges):.1f}")
        print(f"  Displaced    : {n_displ}/{len(graphs)} ({100*n_displ/len(graphs):.1f}%)")
        total += len(graphs)

    print(f"\nTotal graphs saved: {total}")


if __name__ == "__main__":
    main()

