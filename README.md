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

### Recommended production order

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
