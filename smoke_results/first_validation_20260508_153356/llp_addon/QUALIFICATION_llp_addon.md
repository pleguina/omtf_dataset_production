# Sample Qualification: llp_addon

## Dataset

- Role: LLP external validation add-on
- Dataset: /HTo2LongLivedTo2mu2jets_MH-125_MFF-20_CTau-1300mm_TuneCP5_14TeV_pythia8/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD
- Campaign: Phase2Spring24DIGIRECOMiniAOD
- CMSSW/GT inferred tag: 140X_mcRun4_realistic_v4

## One-file smoke input

- Representative file from DAS:
  - /store/mc/Phase2Spring24DIGIRECOMiniAOD/HTo2LongLivedTo2mu2jets_MH-125_MFF-20_CTau-1300mm_TuneCP5_14TeV_pythia8/GEN-SIM-DIGI-RAW-MINIAOD/PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/130000/ea73e0d7-2f4f-44e1-8c2a-a23bd454c205.root

## Qualification table

| Field | Value |
|---|---|
| dataset name | /HTo2LongLivedTo2mu2jets_MH-125_MFF-20_CTau-1300mm_TuneCP5_14TeV_pythia8/Phase2Spring24DIGIRECOMiniAOD-PU200_Trk1GeV_140X_mcRun4_realistic_v4-v1/GEN-SIM-DIGI-RAW-MINIAOD |
| campaign | Phase2Spring24DIGIRECOMiniAOD |
| CMSSW release / GT inferred | 140X_mcRun4_realistic_v4 |
| number of files processed | 1 (smoke target) |
| number of events (dataset summary) | 146000 |
| number of OMTF/TPS windows | TODO (fill after ntuplizer run) |
| mean processors per event | TODO (fill after ntuplizer run) |
| fraction of muons in OMTF overlap | TODO (fill after ntuplizer run) |
| model efficiency | TODO (fill after model eval) |
| current OMTF/GMT efficiency | TODO (fill after reference eval) |
| model / current ratio | TODO |
| background accept (if applicable) | TODO |
| sites | T1_US_FNAL_Tape |
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
