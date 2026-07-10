"""
Recompute the exact helix-perigee transverse impact parameter dxy for
displaced-muon-gun samples, to correct old omtf_nano_*.root / omtf_hits_*.root
files produced before the fixes in commits d2cfb7428b7 / ee49740fc54.

B = 3.811 T is the field convention used by
EMTFTools/ParticleGuns/FlatRandomPtGunProducer2 (and hence by these formulas).
"""

import math

B = 3.811  # Tesla


def true_dxy(pt, charge, phi, vx, vy, B=B):
    """Exact helix-perigee dxy [cm] from generator-level pt/charge/phi and the
    production vertex (vx, vy) [cm]. Works on plain floats.
    """
    rg = -pt / (0.003 * B * charge)
    cx = vx - rg * math.sin(phi)
    cy = vy + rg * math.cos(phi)
    return rg + charge * math.sqrt(cx * cx + cy * cy)


def true_dxy_from_rho_phi(pt, charge, phi, rho, vertex_phi, B=B):
    """Same as true_dxy(), but for the OMTFHitsTree where only muonRho
    (=hypot(vx,vy)) and vertexPhi (=atan2(vy,vx)) are stored instead of raw
    vx/vy.
    """
    vx = rho * math.cos(vertex_phi)
    vy = rho * math.sin(vertex_phi)
    return true_dxy(pt, charge, phi, vx, vy, B=B)


# ---------------------------------------------------------------------------
# PyROOT/RDataFrame helpers to add a corrected column directly from a file.
# ---------------------------------------------------------------------------

def dataframe_with_true_dxy_nano(fname, treename="Events"):
    """Returns an RDataFrame over the NanoAOD tree with a new
    'GenMuon_dXY_true' column (RVec, one entry per gen muon)."""
    import ROOT

    df = ROOT.RDataFrame(treename, fname)
    df = df.Define("rg", "-GenMuon_pt/(0.003*3.811*GenMuon_charge)")
    df = df.Define("cx", "GenMuon_vx - rg*sin(GenMuon_phi)")
    df = df.Define("cy", "GenMuon_vy + rg*cos(GenMuon_phi)")
    df = df.Define("GenMuon_dXY_true", "rg + GenMuon_charge*sqrt(cx*cx+cy*cy)")
    return df


def dataframe_with_true_dxy_hits(fname, treename="simOmtfPhase2Digis/OMTFHitsTree"):
    """Returns an RDataFrame over the OMTFHitsTree with a new
    'muonDxy_true' column (one entry per candidate)."""
    import ROOT

    df = ROOT.RDataFrame(treename, fname)
    df = df.Define("muonVx", "muonRho*cos(vertexPhi)")
    df = df.Define("muonVy", "muonRho*sin(vertexPhi)")
    df = df.Define("rg", "-muonPt/(0.003*3.811*muonCharge)")
    df = df.Define("cx", "muonVx - rg*sin(muonPhi)")
    df = df.Define("cy", "muonVy + rg*cos(muonPhi)")
    df = df.Define("muonDxy_true", "rg + muonCharge*sqrt(cx*cx+cy*cy)")
    return df


if __name__ == "__main__":
    import sys

    fname = sys.argv[1] if len(sys.argv) > 1 else "omtf_nano_sample_0.root"
    df = dataframe_with_true_dxy_nano(fname)
    print(df.AsNumpy(["GenMuon_dXY_true"]))
