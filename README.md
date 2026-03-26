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
  status.sh       #   quick job monitoring helper
scripts/          # Helper scripts (CMSSW setup, config generation, validation)
analysis/         # Post-production analysis scripts and testset plots
fragments/        # Fragment .py files for all datasets
logs/             # HTCondor job logs (git-ignored)
smoke_results/    # Per-dataset smoke-test ROOT files and logs (git-ignored)
```

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
