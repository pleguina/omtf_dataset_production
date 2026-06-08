# Sample Qualification: displaced_midpt

## Dataset

- Role: Displaced controlled medium-pT
- Dataset: /DisplacedMuons_Pt-10To30_Dxy-0To3000-gun/Phase2Fall22DRMiniAOD-PU200_125X_mcRun4_realistic_v2-v1/GEN-SIM-DIGI-RAW-MINIAOD
- Campaign: Phase2Fall22DRMiniAOD
- CMSSW/GT inferred tag: 125X_mcRun4_realistic_v2

## One-file smoke input

- Representative file from DAS:
  - /store/mc/Phase2Fall22DRMiniAOD/DisplacedMuons_Pt-10To30_Dxy-0To3000-gun/GEN-SIM-DIGI-RAW-MINIAOD/PU200_125X_mcRun4_realistic_v2-v1/30000/7124daef-bd99-4446-a276-22c9d79db69b.root

## Qualification table

| Field | Value |
|---|---|
| dataset name | /DisplacedMuons_Pt-10To30_Dxy-0To3000-gun/Phase2Fall22DRMiniAOD-PU200_125X_mcRun4_realistic_v2-v1/GEN-SIM-DIGI-RAW-MINIAOD |
| campaign | Phase2Fall22DRMiniAOD |
| CMSSW release / GT inferred | 125X_mcRun4_realistic_v2 |
| number of files processed | 1 (smoke target) |
| number of events (dataset summary) | 50000 |
| number of OMTF/TPS windows | TODO (fill after ntuplizer run) |
| mean processors per event | TODO (fill after ntuplizer run) |
| fraction of muons in OMTF overlap | TODO (fill after ntuplizer run) |
| model efficiency | TODO (fill after model eval) |
| current OMTF/GMT efficiency | TODO (fill after reference eval) |
| model / current ratio | TODO |
| background accept (if applicable) | TODO |
| sites | T0_CH_CERN_Tape |
| lxplus file accessibility | TODO (xrdcp/xrdfs/cmsRun test) |
| enough info to rerun L1 | TODO (must be proven by cmsRun output branches) |

## Required branch checklist

- [ ] OMTFAllInputTree
- [ ] OMTFHitsTree
- [ ] MuonStubTps
- [ ] MuonStubKmtf
- [ ] GenMuon

## Raw DAS metadata files

- summary: summary.txt
- sites: sites.txt
- one file: files_1.txt
- ten files: files_10.txt
