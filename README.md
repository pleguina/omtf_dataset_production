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

All PU200 samples overlay minimum-bias pileup at **AVE_200_BX_25ns**.

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
All muon-gun samples use flat 1/pT sampling (uniform in 1/pT → unbiased curvature
coverage, no training-time pT reweighting needed).

### B4: noise-only PU200 reference

| Tag | Description | Generator | Target events |
|---|---|---|---|
| **B4** | Noise-only PU200 — no hard-scatter muon | Neutrino gun (PDG 14) | 200 000 |

B4 contains pure pileup occupancy with no signal muon. It is used to set the β
operating point and noise-rejection threshold. It is the **only** retained legacy
B-series dataset; B1–B3 have been superseded by the G-campaign samples.

> **S1–S5 (legacy):** Datasets S1–S5 (single/multi muon, no PU, broad eta, no
> OMTF-window restriction) were produced in the early campaign and are retained
> for reference. They are **deprecated for new training** — use the G and C
> campaigns instead. S1/S2 are superseded by G1/G3 (overlap-restricted,
> OMTF-window enforced). S3/S4/S5 are superseded by G5/G6. S-series lacks the
> eta-window filter and the raw-primitive digi tables present in all G/C nano
> files.

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

## Porting to a Fresh CMSSW Release

The production pipeline is designed to be portable across CMSSW releases. To adapt to a new release (e.g., from `CMSSW_14_2_0_pre2` to `CMSSW_15_0_0`):

### Step 1: Create new CMSSW working area

```bash
# Set up the new release
export CMSSW_VERSION="CMSSW_15_0_0"  # Update version
export SCRAM_ARCH="el9_amd64_gcc12"  # Adjust if compiler changes

scram p CMSSW ${CMSSW_VERSION}
cd ${CMSSW_VERSION}/src

# Clone and rebuild L1Trigger/L1MuNano plugins
git clone https://github.com/cms-sw/cmssw.git upstream
# (or check out from your fork with OMTF customizations)

scram b -j 8

# Verify the build
scram b -j 1 && echo "Build successful"
```

### Step 2: Update run_job.sh

Edit [condor/run_job.sh](condor/run_job.sh) to point to the new CMSSW version:

```bash
# Line ~18: Update CMSSW_VERSION
CMSSW_VERSION="CMSSW_15_0_0"

# Line ~20: Update SCRAM_ARCH if needed (e.g., gcc13 for newer releases)
# export SCRAM_ARCH="el9_amd64_gcc13"
```

### Step 3: Regenerate cmsRun configs

The config files in `configs/` must be regenerated to match the new release's physics content, triggers, and geometry:

```bash
cd scripts
bash generate_configs.sh
```

This runs `cmsDriver.py` for each dataset (GEN, DIGI+PU stages) and applies physics
customizations specific to the new release.

**Key steps in `generate_configs.sh`:**
- Invokes `cmsDriver.py` with era (`Phase2C17I13M9`), geometry (`Extended2026D110`), 
  and global tag (`140X_mcRun4_realistic_v4` — **update if needed**)
- Applies customizations:
  - Tracker alignment workaround (if still needed in new release)
  - OMTF dumper wiring (`customize_omtf_dumper.py`)
- Outputs pairs of configs: `{DATASET}_cfg.py` and `{DATASET}_DR_cfg.py`

### Step 4: Verify geometry and global tag compatibility

**Critical:** Ensure the new release's geometry and global tag are compatible with the 
pileup sample used for overlay.

```bash
# Check the pileup sample's production era
dasgoclient -query "dataset=/MinBias_TuneCP5_14TeV-pythia8/Phase2Spring24GS-140X_mcRun4_realistic_v4-v1/GEN-SIM info"

# Verify the new release can read the sample
# (check CMSSW release notes for geometry/GT changes)
```

**Known compatibility issues:**
- **CMSSW 14.0.6 → 14.2.0_pre2:** Tracker geometry changed (43,708 → 43,600 modules)  
  → Workaround: Add `--customise_commands 'process.trackerGeometry.applyAlignment=cms.bool(False)'`  
  (Already included in generate_configs.sh)

- **New geometry/geometry tag:** Update `Extended2026D110` if new release introduces 
  geometry changes. Update global tag to match pileup sample production.

### Step 5: Update HTCondor submission files

Edit [condor/*.sub](condor/) files if needed:

```bash
# Line ~9: Ensure run_job.sh path is correct
executable = $(TOP)/condor/run_job.sh

# Line ~15: Update CMSSW_VERSION (or remove if it's sourced from run_job.sh)
environment = "CMSSW_VERSION=CMSSW_15_0_0"
```

Most .sub files source run_job.sh, so updating that script propagates the change automatically.

### Step 6: Test on a single job before full relaunch

```bash
# Run a quick smoke test on a small dataset
cd condor
condor_submit -append 'queue 1' G1_pos.sub  # Single job

# Monitor and check output
condor_q pleguina
condor_tail -f 9296651.0  # Replace cluster ID

# Verify new branches are present
root -l smoke_results/omtf_nano_local.root
TBrowser b;
# Navigate to Events tree and confirm new branches (e.g., gen_muon_dz, 
# pileup_nPU, etc.)
```

### Step 7: Relaunch full production

Once smoke test passes:

```bash
cd condor
for sub in *.sub; do
  condor_submit "$sub"
done

# Monitor via scripts/condor_status.sh
bash scripts/condor_status.sh
```

---

### Troubleshooting

| Issue | Solution |
|-------|----------|
| `GeometryMismatch` error | Check if release changed tracker geometry. Apply workaround in generate_configs.sh. |
| `Missing G4SimHits` in TOF branches | Ensure `SimDataFormats/TrackingHit` is available in new release; add input tag defaults. |
| `NanoAOD producer fails` | Rebuild L1Trigger/L1MuNano plugins; check `customize_omtf_dumper.py` for deprecated producer names. |
| `xrdcp upload hangs` | Transient EOS issue; run_job.sh retries automatically (max 3 attempts with 5-min cooldown). |
| `Config generation fails` | Update global tag and era in scripts/generate_configs.sh; verify cmsDriver.py compatibility. |

---

## GMT-visible-stub campaign (G1-G10)

A second production campaign targeted at the Phase-B GMT-visible-stub branch.
This campaign is **additive** — it does not replace the existing S/B datasets.

### Motivation

Two problems found in the early production:
1. **False second candidate from PU** — coherent KMTF noise fires a second
   overlap candidate in displaced+PU200 events (previously observed in B2).
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

## C-campaign: controlled pT × |d0| grid (C1–C28)

Controlled binned muon-gun samples in the OMTF overlap region, designed to
support matched prompt-vs-displaced discrimination studies (WP4 in the displaced
ML action plan). All samples use the OMTF overlap eta window (0.82 < |η| < 1.24),
flat 1/pT sampling within each bin, and 500 events/job.

The full C-campaign covers six matched pT bins (2–5, 5–10, 10–20, 20–50,
50–100, 100–200 GeV) for each of four physics conditions: prompt no-PU,
displaced no-PU, prompt PU200, and displaced PU200. Mild-displaced samples
(C25–C28) additionally cover the ambiguous low-|d0| region.

### C1–C6: Prompt control, no PU

| Tag | Events | Jobs | pT range | |d0| | Description |
|---|---|---|---|---|---|
| C1 | 200k | 400 | 2–5 GeV | <0.05 cm | Prompt overlap muon, no PU |
| C2 | 200k | 400 | 5–10 GeV | <0.05 cm | Prompt overlap muon, no PU |
| C3 | 200k | 400 | 10–20 GeV | <0.05 cm | Prompt overlap muon, no PU |
| C4 | 150k | 300 | 20–50 GeV | <0.05 cm | Prompt overlap muon, no PU |
| C5 | 100k | 200 | 50–100 GeV | <0.05 cm | Prompt overlap muon, no PU |
| C6 | 75k  | 150 | 100–200 GeV | <0.05 cm | Prompt overlap muon, no PU |

### C7–C12: Displaced signal, no PU

| Tag | Events | Jobs | pT range | |d0| range | Description |
|---|---|---|---|---|---|
| C7  | 200k | 400 | 2–5 GeV   | 0.2–50 cm | Displaced overlap muon, no PU |
| C8  | 200k | 400 | 5–10 GeV  | 0.2–50 cm | Displaced overlap muon, no PU |
| C9  | 200k | 400 | 10–20 GeV | 0.2–50 cm | Displaced overlap muon, no PU |
| C10 | 150k | 300 | 20–50 GeV | 0.2–50 cm | Displaced overlap muon, no PU |
| C11 | 100k | 200 | 50–100 GeV | 0.2–50 cm | Displaced overlap muon, no PU |
| C12 | 75k  | 150 | 100–200 GeV | 0.2–50 cm | Displaced overlap muon, no PU |

### C13–C14: Prompt control, low-pT, PU200

| Tag | Events | Jobs | pT range | |d0| | Description |
|---|---|---|---|---|---|
| C13 | 200k | 400 | 2–5 GeV   | <0.05 cm | Prompt overlap muon, PU200 |
| C14 | 200k | 400 | 5–10 GeV  | <0.05 cm | Prompt overlap muon, PU200 |

### C15–C16: Displaced signal, low-pT, PU200

| Tag | Events | Jobs | pT range | |d0| range | Description |
|---|---|---|---|---|---|
| C15 | 200k | 400 | 2–5 GeV   | 0.2–50 cm | Displaced overlap muon, PU200 |
| C16 | 200k | 400 | 5–10 GeV  | 0.2–50 cm | Displaced overlap muon, PU200 |

**Key matched comparison pairs for WP4 (no-PU, clean geometry):**
```
C1 vs C7   (prompt 2-5 GeV vs displaced 2-5 GeV)
C2 vs C8   (prompt 5-10 GeV vs displaced 5-10 GeV)
C3 vs C9   (prompt 10-20 GeV vs displaced 10-20 GeV)
```

**Key matched comparison pairs for WP5 (PU200):**
```
C13 vs C15  (prompt 2-5 GeV vs displaced 2-5 GeV, PU200)
C14 vs C16  (prompt 5-10 GeV vs displaced 5-10 GeV, PU200)
C17 vs C21  (prompt 10-20 GeV vs displaced 10-20 GeV, PU200)
```

### C17–C20: Prompt control, mid–high pT, PU200

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

### Submit C1–C16

```bash
# Submit all 16 new datasets (C1-C12 no-PU: 3700 jobs; C13-C16 PU200: 1600 jobs)
condor_submit condor/C1toC12_noPU_production.sub
condor_submit condor/C13toC16_PU200_production.sub

# Or individually
condor_submit condor/C1_prompt_pt2to5_overlap.sub   # etc.
```

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

## DAS validation samples

CMS DAS datasets used for external validation of the OMTF/GMT ML trigger.
All samples are Phase-2 PU200 GEN-SIM-DIGI-RAW or GEN-SIM-DIGI-RAW-MINIAOD.
Dataset paths were queried on 2026-05-08; check DAS for newer campaigns.

### Signal / efficiency

| Category | Recommended dataset | Notes |
|---|---|---|
| Single muon (gun) | `/SingleMuon_Pt-0To200_Eta-1p4To3p1-gun/Phase2Fall22DRMiniAOD-PU200_125X_mcRun4_realistic_v2-v1/GEN-SIM-DIGI-RAW-MINIAOD` | Phase2Fall22 campaign |
| Single muon high-pT | `/SingleMuon_Pt-200To500_Eta-1p4To3p1-gun/Phase2Fall22DRMiniAOD-PU200_125X_mcRun4_realistic_v2-v1/GEN-SIM-DIGI-RAW-MINIAOD` | |
| Displaced muon (low pT) | `/DisplacedMuons_Pt-2To10_Dxy-0To3000-gun/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD` | Spring24, GT matches production |
| Displaced muon (mid pT) | `/DisplacedMuons_Pt-10To30_Dxy-0To3000-gun/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD` | |
| Displaced muon (high pT) | `/DisplacedMuons_Pt-30To100_Dxy-0To3000-gun/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD` | |
| DY→ℓℓ (low mass) | `/DYToLL_M-10To50_TuneCP5_14TeV-pythia8/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD` | Spring24 |
| DY→ℓℓ (Z peak) | `/DYToLL_M-50_TuneCP5_14TeV-pythia8/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD` | |
| tt̄ | `/TTbar_TuneCP5_14TeV-pythia8/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v2/GEN-SIM-DIGI-RAW-MINIAOD` | |

### Displaced / LLP

| Category | Recommended dataset | cτ / MFF |
|---|---|---|
| H→2LL→4μ | `/HTo2LongLivedTo4mu_MH-125_MFF-12_CTau-900mm_TuneCP5_14TeV-pythia8/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD` | MFF=12, cτ=900 mm |
| H→2LL→4μ | `/HTo2LongLivedTo4mu_MH-125_MFF-25_CTau-1500mm_TuneCP5_14TeV-pythia8/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD` | MFF=25, cτ=1500 mm |
| H→2LL→4μ | `/HTo2LongLivedTo4mu_MH-125_MFF-50_CTau-10000mm_TuneCP5_14TeV-pythia8/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD` | MFF=50, cτ=10 m |
| H→2LL→2μ2j | `/HTo2LongLivedTo2mu2jets_MH-125_MFF-20_CTau-1300mm_TuneCP5_14TeV_pythia8/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD` | MFF=20, cτ=1300 mm |
| H→2LL→2μ2j | `/HTo2LongLivedTo2mu2jets_MH-125_MFF-50_CTau-5000mm_TuneCP5_14TeV_pythia8/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD` | MFF=50, cτ=5 m |

### Background / rate proxy

| Category | Recommended dataset | Notes |
|---|---|---|
| MinBias | `/MinBias_TuneCP5_14TeV-pythia8/Phase2Spring24DIGIRECOMiniAOD-PU200ALCA_140X_mcRun4_realistic_v4-v2/GEN-SIM-DIGI-RAW-MINIAOD` | Spring24 |
| QCD (EM-enriched, 120–170 GeV) | `/QCD_Pt-120To170_EMEnriched_TuneCP5_14TeV-pythia8/Phase2Spring23DIGIRECOMiniAOD-PU200_Trk1GeV_131X_mcRun4_realistic_v5-v1/GEN-SIM-DIGI-RAW-MINIAOD` | representative HT bin |

> **NuGun not found on DAS** for Phase-2 PU200 (2026-05-08 search). Use MinBias
> or QCD as rate-proxy substitutes until a NuGun Phase-2 dataset is published.

### Query patterns

```bash
# Reproduce the search
dasgoclient --query "dataset dataset=/*Displaced*Muon*/*Phase2*PU200*/*"
dasgoclient --query "dataset dataset=/*LongLived*/*Phase2*PU200*/*"
dasgoclient --query "dataset dataset=/DYToLL*/*Phase2*PU200*/*"
dasgoclient --query "dataset dataset=/TT*/*Phase2*PU200*/*"
dasgoclient --query "dataset dataset=/MinBias*/*Phase2*PU200*/*"
dasgoclient --query "dataset dataset=/QCD*/*Phase2*PU200*/*"
```

Full search results are archived in `das_validation_sample_search_20260508_150126/`
(workspace root). See `SEARCH_RESULTS_STATUS.md` in that directory for the query
status table and first-match examples for all 16 queries run.

### Generation scripts

```bash
scripts/submit_das_validation.sh    # submit DAS-based validation jobs
condor/run_das_job.sh               # worker script for DAS input jobs
scripts/generate_configs.sh         # generate G-campaign cmsRun configs
scripts/create_condor_subs.sh       # create Condor submit files
```

Full DAS dataset paths and G1-G10 campaign details are also maintained in
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

## Production status (as of 2026-07-06)

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

### C-campaign (C1–C16) — submitted 2026-07-05

Condor clusters: 9295133 (C1–C12 no-PU, 3700 jobs) and 9295134 (C13–C16 PU200, 1600 jobs).
Same `customize_omtf_dumper.py` as C17–C28 — full digi tables included.

| Dataset | Jobs | Target events | Status |
|---|---|---|---|
| C1_prompt_pt2to5_overlap | 400 | 200k | 🔄 In progress |
| C2_prompt_pt5to10_overlap | 400 | 200k | 🔄 In progress |
| C3_prompt_pt10to20_overlap | 400 | 200k | 🔄 In progress |
| C4_prompt_pt20to50_overlap | 300 | 150k | 🔄 In progress |
| C5_prompt_pt50to100_overlap | 200 | 100k | 🔄 In progress |
| C6_prompt_pt100to200_overlap | 150 | 75k | 🔄 In progress |
| C7_disp_pt2to5_overlap | 400 | 200k | 🔄 In progress |
| C8_disp_pt5to10_overlap | 400 | 200k | 🔄 In progress |
| C9_disp_pt10to20_overlap | 400 | 200k | 🔄 In progress |
| C10_disp_pt20to50_overlap | 300 | 150k | 🔄 In progress |
| C11_disp_pt50to100_overlap | 200 | 100k | 🔄 In progress |
| C12_disp_pt100to200_overlap | 150 | 75k | 🔄 In progress |
| C13_prompt_pt2to5_overlap_PU200 | 400 | 200k | 🔄 In progress |
| C14_prompt_pt5to10_overlap_PU200 | 400 | 200k | 🔄 In progress |
| C15_disp_pt2to5_overlap_PU200 | 400 | 200k | 🔄 In progress |
| C16_disp_pt5to10_overlap_PU200 | 400 | 200k | 🔄 In progress |

### B4 (noise-only PU200) — re-submitted 2026-07-06 with full digi tables

Previous `B4.sub` had a bug (3 arguments instead of 4) and never produced EOS
output. Replaced by `B4_digi.sub`.

| Dataset | Jobs | Target events | Condor cluster | Status |
|---|---|---|---|---|
| B4 (pure noise PU200) | 400 | 200k | 9295685 | 🔄 In progress |

> **Note:** B4 output lands in `prod/B4/` (same directory as before).
> The old empty `B4/` directory on EOS will be populated by this run.
> Output schema identical to C1–C28: full 5-collection digi tables + stubs + `GenMuon` (neutrino, effectively empty signal truth).
