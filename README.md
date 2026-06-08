# omtf-hecin-dataset-production

HTCondor-based Monte Carlo dataset generation pipeline for CMS Phase-2 OMTF
(Overlap Muon Track Finder) machine-learning studies. Produces GEN–SIM–DIGI–RAW
ntuples as ROOT files, later used to train and benchmark GNN-based muon
reconstruction algorithms.

---

## Software environment

| Item | Value |
|---|---|
| CMSSW release | `CMSSW_14_2_0_pre2` |
| Architecture | `el9_amd64_gcc12` (RHEL 9 / GCC 12) |
| Geometry | `Extended2026D110` |
| Era | `Phase2C17I13M9` |
| Global Tag | `140X_mcRun4_realistic_v4` |
| Beam spot | `HLLHC14TeV` |

The geometry and global tag are chosen to match the `Phase2Spring24GS` MinBias
pileup sample (see [Pileup](#pileup) below).

---

## Generation chain

Each dataset is produced in a single `cmsRun` step combining:

```
GEN → SIM → DIGI:pdigi_valid → L1TrackTrigger → L1 → DIGI2RAW → HLT:@fake2
```

- **GEN**: particle gun (configurable per dataset, see [Datasets](#datasets))
  fires muons according to the fragment configuration.
- **SIM**: Geant4 propagates each muon through the full Phase-2 CMS detector
  geometry; produces energy deposits in all subdetectors including the OMTF
  barrel/overlap stations (DT, CSC, RPC).
- **DIGI / L1**: digitisation of Geant4 hits, Level-1 trigger emulation, and
  pileup overlay (for background samples). The OMTF emulator runs inside this
  step and produces L1 muon candidates.
- **DIGI2RAW / HLT**: packs raw detector data and runs the HLT skeleton
  (`@fake2` — no full HLT selection).

The output tier is `GEN-SIM-DIGI-RAW` (`FEVTSIM` event content).
An OMTF Dumper module (`customize_omtf_dumper.py`) is appended at runtime to
write a per-event ntuple (ROOT TTree) containing L1 muon hits, candidates, and
the generator-level truth.

---

## Pileup

Background samples (B1–B3) overlay minimum-bias pileup at **AVE_200_BX_25ns**.

**Sample used:**
```
/MinBias_TuneCP5_14TeV-pythia8/Phase2Spring24GS-140X_mcRun4_realistic_v4-v1/GEN-SIM
```
(CMS McM prep-id `PPD-Phase2Spring24GS-00002`)

**Why this sample / why D110?**
The pileup sample was produced with `CMSSW_14_0_6` using geometry
`Extended2026D110` and global tag `140X_mcRun4_realistic_v4`. The GEN–SIM step
of every sample in this repository uses the **same** geometry and global tag so
that the detector layouts and alignment payloads are consistent between the hard
scatter and the pileup overlay. Using a mismatched geometry would corrupt hit
positions in the mixed events.

**DIGI tracker-alignment workaround:**
`Extended2026D110` was modified between CMSSW 14.0.6 and 14.2.0\_pre2 (43 708 →
43 600 tracker modules). The T33 tracker-alignment payload in the conditions DB
has 43 708 entries and triggers a fatal `GeometryMismatch` in 14.2.0\_pre2.
The configs therefore add:
```
--customise_commands 'process.trackerGeometry.applyAlignment=cms.bool(False)'
```
This disables tracker misalignment and uses ideal geometry, which is acceptable
for OMTF L1 trigger studies that do not depend on tracker alignment.

---

## Datasets

OMTF acceptance used throughout: **0.82 < |η| < 1.24**.
All signal samples use flat 1/pT sampling (uniform in 1/pT → unbiased curvature
coverage, no training-time pT reweighting needed).

### Signal samples (no pileup)

| Tag | Description | Generator | pT range | Multiplicity / event | Target events |
|---|---|---|---|---|---|
| **S1** | Single prompt muon | `FlatRandomOneOverPtGunProducer` | 2–200 GeV | 1 muon | 500 000 |
| **S2** | Single displaced muon, flat d₀ | `FlatRandomPtGunProducer2` | 2–200 GeV | 1 muon | 500 000 |
| **S3** | Two prompt muons, same OMTF window | `FlatRandomPtGunProducer2` | 2–100 GeV | 2 muons | 250 000 |
| **S4** | Three prompt muons, same OMTF window | `FlatRandomPtGunProducer2` | 5–80 GeV | 3 muons | 150 000 |
| **S5** | Two displaced muons (same event), flat d₀ | `FlatRandomPtGunProducer2` | 2–200 GeV | 2 muons | 150 000 |

**S2 displacement**: d₀ flat uniform in [0, 50] cm, Lxy capped at 200 cm
(production vertex must be inside MB1 at r ≈ 231 cm for the muon to produce OMTF
hits).

**S3 / S4 same-window constraint**: both/all muons' φ is restricted to a single
60°-wide OMTF processor window (φ ∈ [−π/6, +π/6]) to ensure they hit the same
processor and exercise multi-track disambiguation.

**S5 displacement**: d₀ flat uniform in [0, 30] cm per muon; two independent
muons per event (unlike S2 which has one); designed to stress Object Condensation
repulsion loss.

### Background samples (PU 200)

| Tag | Description | Generator | pT range | Multiplicity / event | Target events |
|---|---|---|---|---|---|
| **B1** | Single prompt muon + PU200 | `FlatRandomOneOverPtGunProducer` | 2–200 GeV | 1 muon + PU | 200 000 |
| **B2** | Single displaced muon + PU200, flat d₀ | `FlatRandomPtGunProducer2` | 2–200 GeV | 1 muon + PU | 200 000 |
| **B3** | Two prompt muons, same window + PU200 | `FlatRandomPtGunProducer2` | 2–100 GeV | 2 muons + PU | 100 000 |

B2 uses the same d₀ range as S2 ([0, 50] cm) to provide a displaced background
baseline. B3 uses the same same-window φ constraint as S3.

---

## Repository layout

```
fragments/        # cmsDriver generator fragments (one .py per dataset)
configs/          # Full cmsRun configs generated from fragments + cmsDriver
  *_cfg.py        #   base configs (patched at runtime by run_job.sh)
  customize_omtf_dumper.py  # OMTF ntuple writer appended to every config
condor/           # HTCondor submission
  run_job.sh      #   worker script (patches config, runs cmsRun, uploads to EOS)
  full_production.sub  #   submit file for all 48 production jobs
  G1.sub ... G10_neg.sub    #   per-dataset submit files for G-campaign
  GMT_overlap_production.sub  #  grouped submit for all 4000 G-campaign jobs
  status.sh       #   quick job monitoring helper
scripts/          # Helper scripts (CMSSW setup, config generation, submission)
  audit/          #   Pre-production fragment and generator-level audit scripts
  omtf_gmt/       #   GMT-branch stub-level and truth-transfer audit scripts
analysis/         # Post-production analysis scripts and testset plots
fragments/        # Fragment .py files for all datasets (S*, B*, G*)
logs/             # HTCondor job logs (git-ignored)
smoke_results/    # Optional local diagnostics and temporary reports (git-ignored)
```

For the ROOT tree and branch map produced by the dumper, see [scripts/ROOT_BRANCHES.md](scripts/ROOT_BRANCHES.md).

---

## Quick start

```bash
# 1. Valid proxy (7+ days for production)
voms-proxy-init --voms cms --valid 240:00

# 2. Set up CMSSW working area (first time only)
bash scripts/setup_cmssw.sh

# 3. Submit all production jobs
condor_submit condor/full_production.sub

# 4. Monitor
bash condor/status.sh
```

Output ROOT files are uploaded to EOS:
```
/eos/user/<initial>/<username>/omtf_hecin_datasets/prod/<DATASET>/omtf_hits_<DATASET>_<ProcId>.root
```

---

## GMT-visible-stub campaign (G1-G10)

A second production campaign targeted at the Phase-B GMT-visible-stub branch.
This campaign is **additive** — it does not replace the existing S/B datasets.

### Motivation

Two problems found in the existing production:
1. **B2 false second candidate from PU** — coherent KMTF noise fires a second
   overlap candidate in B2 displaced+PU200 events.
2. **Out-of-domain barrel tracks** — real KMTF barrel muons (|η| < 0.75) can
   create stubs that the model incorrectly promotes to overlap candidates.
3. **Out-of-domain high-eta endcap tracks** — real endcap-like muons
  (1.30 < |η| < 1.80) can also look signal-like to TPS-driven models but are
  outside OMTF overlap target acceptance.

### Dataset summary

| Tag | Events | Jobs | PU | Purpose |
|-----|-------:|-----:|-----|---------|
| G1  | 300k   | 600  | no  | Clean 1-candidate prompt overlap |
| G2  | 300k   | 600  | yes | 1-candidate prompt under PU |
| G3  | 250k   | 500  | no  | Clean 1-candidate displaced overlap |
| G4  | 300k   | 600  | yes | 1-candidate displaced under PU |
| G5  | 200k   | 400  | yes | 2 displaced candidates under PU |
| G6  | 200k   | 400  | yes | 3-candidate prompt under PU |
| G7  | 150k   | 300  | no  | Hard negative: barrel track, overlap target = 0 |
| G8  | 300k   | 600  | yes | Hard negative with PU |
| G9_pos | 75k | 150 | no  | Hard negative: high-eta positive endcap side |
| G9_neg | 75k | 150 | no  | Hard negative: high-eta negative endcap side |
| G10_pos | 150k | 300 | yes | High-eta hard negative with PU (positive side) |
| G10_neg | 150k | 300 | yes | High-eta hard negative with PU (negative side) |

Total: 2,600,000 events / 5,200 Condor jobs at 500 events/job.

Split-side convention for endcap hard negatives:
- Keep production separated as `G9_pos`, `G9_neg`, `G10_pos`, `G10_neg`.
- Downstream cache alias expansion should map `G9 -> G9_pos + G9_neg` and
  `G10 -> G10_pos + G10_neg`.

---

## Extended G-campaign (G13–G17)

Additional control and signal datasets added to support the displaced-muon
recovery study. All use 500 events/job and the same CMSSW environment as G1–G10.

| Tag | Events | Jobs | PU200 | Description |
|---|---|---|---|---|
| G13_neg | 100k | 200 | no | Prompt single muon, transition region, negative η (−1.6 < η < −1.2) |
| G13_pos | 100k | 200 | no | Prompt single muon, transition region, positive η (1.2 < η < 1.6) |
| G14_neg | 100k | 200 | yes | Prompt single muon + PU200, transition region, negative η |
| G14_pos | 100k | 200 | yes | Prompt single muon + PU200, transition region, positive η |
| G15_neg | 100k | 200 | no | Prompt single muon, endcap, negative η (−2.4 < η < −1.6) |
| G15_pos | 100k | 200 | no | Prompt single muon, endcap, positive η (1.6 < η < 2.4) |
| G16_neg | 100k | 200 | yes | Prompt single muon + PU200, endcap, negative η |
| G16_pos | 100k | 200 | yes | Prompt single muon + PU200, endcap, positive η |
| G17 | 150k | 300 | no | Two displaced muons, OMTF overlap (0.82 < |η| < 1.24) |

**Purpose:**
- G13/G14 (transition guard band): control samples for muons migrating between
  overlap and endcap regions; needed because displaced tracks may not respect
  nominal regional boundaries.
- G15/G16 (endcap control): hard-negative endcap muons that should not fire the
  overlap displaced trigger.
- G17 (two displaced): exercises multi-candidate separated vertex topology;
  extends G5 (which has PU200) with a clean no-PU geometry sample.

---

## C-campaign: controlled pT × |d0| grid (C17–C28)

Controlled binned muon-gun samples in the OMTF overlap region, designed to
support matched prompt-vs-displaced discrimination studies (WP4 in the displaced
ML action plan). All samples use the OMTF overlap eta window (0.82 < |η| < 1.24),
flat 1/pT sampling within each bin, and 500 events/job.

### Prompt control samples (no displacement, PU200)

| Tag | Events | Jobs | pT range | Description |
|---|---|---|---|---|
| C17 | 200k | 400 | 10–20 GeV | Prompt overlap muon, PU200 |
| C18 | 150k | 300 | 20–50 GeV | Prompt overlap muon, PU200 |
| C19 | 100k | 200 | 50–100 GeV | Prompt overlap muon, PU200 |
| C20 | 75k  | 150 | 100–200 GeV | Prompt overlap muon, PU200 |

### Displaced signal samples (PU200)

| Tag | Events | Jobs | pT range | |d0| range | Description |
|---|---|---|---|---|---|
| C21 | 200k | 400 | 10–20 GeV | 0.2–300 cm | Displaced overlap muon, PU200 |
| C22 | 150k | 300 | 20–50 GeV | 0.2–300 cm | Displaced overlap muon, PU200 |
| C23 | 100k | 200 | 50–100 GeV | 0.2–300 cm | Displaced overlap muon, PU200 |
| C24 | 75k  | 150 | 100–200 GeV | 0.2–300 cm | Displaced overlap muon, PU200 |

### Mild-displaced samples (no PU and PU200)

| Tag | Events | Jobs | pT range | |d0| range | PU | Description |
|---|---|---|---|---|---|---|
| C25 | 200k | 400 | 2–5 GeV | 0.05–5 cm | no | Mild displaced, no PU |
| C26 | 200k | 400 | 5–10 GeV | 0.05–5 cm | no | Mild displaced, no PU |
| C27 | 200k | 400 | 2–5 GeV | 0.05–5 cm | yes | Mild displaced, PU200 |
| C28 | 200k | 400 | 5–10 GeV | 0.05–5 cm | yes | Mild displaced, PU200 |

**Why C25/C26 (no PU):** The mild-displaced no-PU samples provide the cleanest
geometry for the prompt-origin compatibility study (phi0 proxy, curvature spread)
without PU contamination.

**Key use case:** The primary analysis pairing is:
```
prompt low-pT (C17/C18) vs displaced low-pT (C21/C22 or C25/C26)
same pT bin, same eta bin, same PU condition
```
This directly probes whether displaced muons can be separated from prompt low-pT
muons using trigger-level geometric variables.

### Recommended production order

**Wave 1 (highest priority):** G8, G2

**Wave 1 (highest priority):** G8, G2  
**Wave 2:** G4, G6  
**Wave 3:** G1, G3, G5, G7

### Submit

```bash
# Individual dataset (recommended for priority-order waves)
condor_submit condor/G10_pos.sub
condor_submit condor/G10_neg.sub

# All 5200 jobs at once
condor_submit condor/GMT_overlap_production.sub
```

---

## DAS Generation (production only)

This repository also supports generation from CMS DAS GEN-SIM-DIGI-RAW-MINIAOD
inputs for external production samples.

Current DAS sample set:

- minbias
- displaced_lowpt
- displaced_midpt
- dy_prompt
- llp_addon
- single_muon_flatpt

Generation scripts (no validation/smoke workflow):

```bash
scripts/submit_das_validation.sh
condor/run_das_job.sh
```

G-campaign generation scripts:

```bash
scripts/generate_configs.sh
scripts/create_condor_subs.sh
```

Full DAS dataset paths and G1-G10 campaign details are maintained in
`DATASETS_INFO.txt`.

### Fragment safety rules

- Single-muon datasets (G1/G2/G3/G4/G7/G8/G9_pos/G9_neg/G10_pos/G10_neg):
  exactly **one** entry in `PartID`.
- Multi-muon datasets (G5: 2 entries, G6: 3 entries): multiple entries are
  **intentional**.
- `etaFilter` is defined in G1–G4 and **must** appear in
  `ProductionFilterSequence`.
- `AddAntiParticle = False` for all G-campaign fragments.

See `DATASETS_INFO.txt` for full per-dataset specifications.

---

## Production status (as of 2026-06-08)

EOS base path: `/eos/user/p/pleguina/omtf_hecin_datasets/prod/`

### G-campaign (G1–G17)

| Dataset | Files | Target | Status |
|---|---|---|---|
| G1_neg | 300 | 300 | ✅ Complete |
| G1_pos | 300 | 300 | ✅ Complete |
| G3_neg | 250 | 250 | ✅ Complete |
| G3_pos | 250 | 250 | ✅ Complete |
| G4_neg | 300 | 300 | ✅ Complete |
| G4_pos | 300 | 300 | ✅ Complete |
| G5_neg | 200 | 200 | ✅ Complete |
| G5_pos | 200 | 200 | ✅ Complete |
| G6_neg | 200 | 200 | ✅ Complete |
| G6_pos | 200 | 200 | ✅ Complete |
| G7 | 300 | 300 | ✅ Complete |
| G8 | 600 | 600 | ✅ Complete |
| G9_neg | 150 | 150 | ✅ Complete |
| G9_pos | 150 | 150 | ✅ Complete |
| G10_neg | 300 | 300 | ✅ Complete |
| G10_pos | ~297 | 300 | 🔄 Near-complete (sporadic PU200 failures) |
| G13_neg | 200 | 200 | ✅ Complete |
| G13_pos | 200 | 200 | ✅ Complete |
| G14_neg | ~176 | 200 | 🔄 Near-complete (sporadic PU200 failures) |
| G14_pos | ~180 | 200 | 🔄 Near-complete (sporadic PU200 failures) |
| G15_neg | 200 | 200 | ✅ Complete |
| G15_pos | 200 | 200 | ✅ Complete |
| G16_neg | ~186 | 200 | 🔄 Near-complete (sporadic PU200 failures) |
| G16_pos | ~168 | 200 | 🔄 Near-complete (sporadic PU200 failures) |
| G17 | 300 | 300 | ✅ Complete |

Note: G2_neg/G2_pos not resubmitted (kept from prior production run).
Note: G1/G2/G3/G4/G5/G6 merged `.sub` files are redundant with `_pos` variants (same eta range); only neg/pos splits are used.

### C-campaign (C17–C28)

| Dataset | Files | Target | Status |
|---|---|---|---|
| C17_prompt_pt10to20_overlap_PU200 | 400 | 400 | ✅ Complete |
| C18_prompt_pt20to50_overlap_PU200 | 300 | 300 | ✅ Complete |
| C19_prompt_pt50to100_overlap_PU200 | 200 | 200 | ✅ Complete |
| C20_prompt_pt100to200_overlap_PU200 | 150 | 150 | ✅ Complete |
| C21_disp_pt10to20_overlap_PU200 | 400 | 400 | ✅ Complete |
| C22_disp_pt20to50_overlap_PU200 | 300 | 300 | ✅ Complete |
| C23_disp_pt50to100_overlap_PU200 | 200 | 200 | ✅ Complete |
| C24_disp_pt100to200_overlap_PU200 | 150 | 150 | ✅ Complete |
| C25_mild_disp_pt2to5_overlap | 400 | 400 | ✅ Complete |
| C26_mild_disp_pt5to10_overlap | 400 | 400 | ✅ Complete |
| C27_mild_disp_pt2to5_overlap_PU200 | 400 | 400 | ✅ Complete |
| C28_mild_disp_pt5to10_overlap_PU200 | 400 | 400 | ✅ Complete |

All C* datasets produced with the corrected `customize_omtf_dumper.py` including
all 5 primitive digi collections (DTPhiDigi, Ph2DTPhiDigi, Ph2DTThDigi,
CSCLctDigi, RPCDigi) plus MuonStubKmtf, MuonStubTps tables (135 branches total).
