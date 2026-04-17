# ROOT Ntuple Branch Reference

Complete branch listing from the CMSSW_14_2_0_pre2 production jobs.
Inspected from: `omtf_hits_S1_0.root` and `omtf_nano_S1_0.root`
EOS path: `/eos/user/p/pleguina/omtf_hecin_datasets/prod/<SAMPLE>/`

The notes below stay brief for obvious bookkeeping fields and add extra physics context only where the branch name alone is easy to misread.

---

## omtf_hits_<SAMPLE>_<N>.root

Contains two trees inside directory `simOmtfPhase2Digis/`.

---

### OMTFAllInputTree  (one entry per processor-window per event)

**Use for: GNN training**

| Branch | ROOT type | Description |
|--------|-----------|-------------|
| `reg_eventNum` | `UInt_t` | Event number (matches NanoAOD `event` field) |
| `reg_iProcessor` | `UChar_t` | OMTF processor index 0–11 (120° sector) |
| `reg_mtfType` | `Char_t` | Processor type (OMTF=1, BMTF=2, EMTF=3) |
| `reg_stub_layer` | `vector<signed char>` | Logic layer index 0–17 per stub |
| `reg_stub_phiHw` | `vector<short>` | Absolute trigger phi of the stub in OMTF hardware bins (5400 bins / 2π) |
| `reg_stub_phiBHw` | `vector<short>` | DT bending-angle proxy in hardware units; this is the local curvature input used by the trigger, and it is 0 for CSC/RPC stubs |
| `reg_stub_etaHw` | `vector<signed char>` | Coarse trigger eta code for the stub; physical eta = etaHw × 0.010875 |
| `reg_stub_r` | `vector<short>` | Radial distance in cm |
| `reg_stub_quality` | `vector<signed char>` | Stub quality word from the muon trigger primitive, on the 0–15 hardware scale |
| `reg_stub_type` | `vector<signed char>` | `MuonStub::Type` enum identifying the detector technology/subtype that produced the stub |
| `reg_stub_bx` | `vector<signed char>` | BX offset relative to the in-time bunch crossing (typically −3..+3; 0=in-time) |
| `reg_stub_trackId` | `vector<signed char>` | **0 = noise/PU or no usable truth link; 1..K = 1-indexed gen-muon label** from SimTrack matching |
| `reg_stub_ambiguous` | `vector<unsigned char>` | 1 when the SimTrack digi votes do not give a clear majority for this stub, so the truth label is low-confidence |

All `reg_stub_*` vectors have the same length for a given entry (one element per stub in that window).

---

### OMTFHitsTree  (one entry per OMTF candidate matched to a gen-muon)

**Use for: trigger efficiency, pT resolution, fake rate studies**

#### Gen-muon truth (scalars)

| Branch | ROOT type | Description |
|--------|-----------|-------------|
| `eventNum` | `UInt_t` | Event number |
| `muonEvent` | `Short_t` | Which muon in the event (index) |
| `muonPt` | `Float_t` | Gen-muon pT [GeV] |
| `muonEta` | `Float_t` | Gen-muon η at vertex |
| `muonPhi` | `Float_t` | Gen-muon φ at vertex |
| `muonPropEta` | `Float_t` | Gen-muon η extrapolated from the production vertex to muon station 2, i.e. the angle actually used when comparing to trigger primitives |
| `muonPropPhi` | `Float_t` | Gen-muon φ extrapolated from the production vertex to muon station 2, i.e. the trigger-facing azimuth at that station |
| `muonCharge` | `Char_t` | Gen-muon charge (±1) |
| `muonDxy` | `Float_t` | Gen-muon transverse impact parameter d₀ [cm] with sign convention from the generator / tracking-particle definition |
| `muonRho` | `Float_t` | Radial distance [cm] of the muon production or decay vertex from the beamline in the transverse plane |
| `parentPdgId` | `Short_t` | PDG ID of the generator-level mother particle that produced the muon |
| `vertexEta` | `Float_t` | Pseudorapidity of the production / decay vertex direction as seen from the origin |
| `vertexPhi` | `Float_t` | Azimuth of the production / decay vertex direction as seen from the origin |

#### OMTF candidate output (scalars)

| Branch | ROOT type | Description |
|--------|-----------|-------------|
| `omtfPt` | `Float_t` | OMTF assigned pT [GeV] (after LUT pT assignment) |
| `omtfUPt` | `Float_t` | OMTF pT estimate before the final curvature / quality constraint is applied [GeV] |
| `omtfEta` | `Float_t` | OMTF candidate η |
| `omtfPhi` | `Float_t` | OMTF candidate φ |
| `omtfCharge` | `Char_t` | OMTF charge (±1) |
| `omtfHwEta` | `Short_t` | OMTF candidate η after trigger quantization to hardware units |
| `omtfProcessor` | `Char_t` | OMTF processor index |
| `omtfScore` | `Short_t` | Pattern matcher score (higher = better match) |
| `omtfQuality` | `Char_t` | OMTF quality word |
| `omtfRefLayer` | `Char_t` | Reference layer used for pT assignment |
| `omtfRefHitNum` | `Char_t` | Reference hit index |
| `omtfRefHitPhi` | `Short_t` | Reference hit phi HW |
| `omtfFiredLayers` | `UInt_t` | Bitmask of fired logic layers (bit k = layer k fired) |
| `killed` | `Bool_t` | True if this candidate was ghost-busted (suppressed by a better candidate in same region) |

#### Per-candidate stubs (vectors — one element per stub on the candidate track)

| Branch | ROOT type | Description |
|--------|-----------|-------------|
| `hits` | `vector<unsigned long>` | Packed uint64: bits 0–7=layer, 8–15=quality, 16–23=etaHw, 24–31=valid, 32–47=deltaR, 48–63=phiDist |
| `hits_phiHw` | `vector<short>` | Absolute phi HW (same stub, unpacked convenience branch) |
| `hits_phiBHw` | `vector<short>` | DT bending angle HW (0 for non-DT) |
| `hits_r` | `vector<short>` | Radial distance cm |
| `hits_type` | `vector<signed char>` | MuonStub::Type |
| `hits_bx` | `vector<signed char>` | BX offset |

#### Matching quality (scalars)

| Branch | ROOT type | Description |
|--------|-----------|-------------|
| `deltaEta` | `Float_t` | Δη between OMTF candidate and gen-muon |
| `deltaPhi` | `Float_t` | Δφ between OMTF candidate and gen-muon |

---

## omtf_nano_<SAMPLE>_<N>.root

Standard CMS NanoAOD format; main tree: `Events` (one entry per event).

### Event bookkeeping

| Branch | ROOT type | Description |
|--------|-----------|-------------|
| `run` | `UInt_t` | Run number |
| `luminosityBlock` | `UInt_t` | Lumi block |
| `event` | `ULong64_t` | Event number (matches OMTFAllInputTree `reg_eventNum`) |
| `bunchCrossing` | `UInt_t` | BX number |
| `orbitNumber` | `UInt_t` | Orbit number |

### Gen-level muons (`nGenMuon` entries per event)

| Branch | ROOT type | Description |
|--------|-----------|-------------|
| `nGenMuon` | `Int_t` | Number of gen muons in event |
| `GenMuon_pt` | `Float_t` | pT [GeV] |
| `GenMuon_eta` | `Float_t` | η |
| `GenMuon_phi` | `Float_t` | φ |
| `GenMuon_charge` | `Int_t` | ±1 |
| `GenMuon_pdgId` | `Int_t` | PDG ID (±13 for muons) |
| `GenMuon_status` | `Int_t` | Pythia8 status code |
| `GenMuon_dXY` | `Float_t` | Transverse impact parameter d₀ [cm] |
| `GenMuon_lXY` | `Float_t` | Transverse decay length [cm] |
| `GenMuon_vx` | `Float_t` | Production vertex x [cm] |
| `GenMuon_vy` | `Float_t` | Production vertex y [cm] |
| `GenMuon_vz` | `Float_t` | Production vertex z [cm] |
| `GenMuon_etaSt1` | `Float_t` | η propagated to 1st muon station |
| `GenMuon_etaSt2` | `Float_t` | η propagated to 2nd muon station |
| `GenMuon_phiSt1` | `Float_t` | φ propagated to 1st muon station |
| `GenMuon_phiSt2` | `Float_t` | φ propagated to 2nd muon station |

### OMTF GMT output (`nomtf` entries per event)

These are the OMTF candidates **after ghost busting**, as sent to the GMT.

| Branch | ROOT type | Description |
|--------|-----------|-------------|
| `nomtf` | `Int_t` | Number of OMTF candidates |
| `omtf_hwPt` | `Short_t` | Final GMT-facing pT code in hardware units |
| `omtf_hwPtUnc` | `Short_t` | Unconstrained pT code before the final trigger constraint is imposed |
| `omtf_hwEta` | `Short_t` | Quantized OMTF η code used by the trigger |
| `omtf_hwPhi` | `Short_t` | Quantized OMTF φ code used by the trigger |
| `omtf_hwQual` | `Short_t` | Quality word |
| `omtf_hwDXY` | `Short_t` | Hardware displacement class, i.e. the trigger's coarse encoding of transverse impact parameter / $d_{xy}$ |
| `omtf_Q` | `Short_t` | Charge |
| `omtf_processor` | `Short_t` | Processor index |
| `omtf_muIdx` | `Short_t` | Index into GenMuon collection (−1 if unmatched) |

### Muon stubs — KMTF format (`nMuonStubKmtf` entries per event)

| Branch | ROOT type | Description |
|--------|-----------|-------------|
| `nMuonStubKmtf` | `Int_t` | Number of stubs |
| `MuonStubKmtf_isBarrel` | `Bool_t` | True if DT/RPC barrel |
| `MuonStubKmtf_isEndcap` | `Bool_t` | True if CSC/RPC endcap |
| `MuonStubKmtf_bxNum` | `Short_t` | BX offset |
| `MuonStubKmtf_coord1` | `Short_t` | Primary coordinate (phi HW) |
| `MuonStubKmtf_coord2` | `Short_t` | Secondary coordinate (phiB HW for DT) |
| `MuonStubKmtf_eta1` | `Short_t` | First eta measurement HW |
| `MuonStubKmtf_eta2` | `Short_t` | Second eta measurement HW |
| `MuonStubKmtf_etaQuality` | `Short_t` | Eta quality |
| `MuonStubKmtf_etaRegion` | `Short_t` | Eta region index |
| `MuonStubKmtf_phiRegion` | `Short_t` | Phi region index |
| `MuonStubKmtf_quality` | `Short_t` | Stub quality |
| `MuonStubKmtf_stubType` | `Short_t` | Stub type |
| `MuonStubKmtf_tfLayer` | `Short_t` | TF logic layer |
| `MuonStubKmtf_depthRegion` | `Short_t` | Depth region |
| `MuonStubKmtf_id` | `Short_t` | Stub ID |
| `MuonStubKmtf_addr` | `Int_t` | Address |
| `MuonStubKmtf_kmtf_addr` | `Int_t` | KMTF-specific address |
| `MuonStubKmtf_offlineCoord1` | `Float_t` | Offline-calibrated version of `coord1`, i.e. the same trigger-primitive coordinate expressed with geometry corrections applied |
| `MuonStubKmtf_offlineCoord2` | `Float_t` | Offline-calibrated version of `coord2` (the companion coordinate or bending term, depending on detector technology) |
| `MuonStubKmtf_offlineEta1` | `Float_t` | Offline-calibrated version of the first eta measurement |
| `MuonStubKmtf_offlineEta2` | `Float_t` | Offline-calibrated version of the second eta measurement |

### Muon stubs — TPS format (`nMuonStubTps` entries per event)

Same schema as KMTF above with `MuonStubTps_` prefix.  TPS stubs are the
raw trigger primitive segments before KMTF processing.

---

## Joining the two files

To attach gen-muon kinematics from NanoAOD to `OMTFAllInputTree` stubs:

```python
# Both files have matching event numbers
# OMTFAllInputTree: reg_eventNum  (UInt_t)
# NanoAOD Events:  event          (ULong64_t)

# For a gen-muon regression target on a stub:
#   reg_stub_trackId > 0  →  muon index = reg_stub_trackId - 1  (0-indexed into GenMuon_*)
#   reg_stub_trackId == 0 →  noise; fill targets with 0.0 and mask in loss

# Note: reg_eventNum comes from the CMSSW event ID modulo 2^32.
# NanoAOD 'event' is the full 64-bit ID. Cast to uint32 before matching.
```

To find which OMTF candidate in `OMTFHitsTree` corresponds to a given
`OMTFAllInputTree` window: match on `(eventNum, omtfProcessor)` vs `(reg_eventNum, reg_iProcessor)`.
