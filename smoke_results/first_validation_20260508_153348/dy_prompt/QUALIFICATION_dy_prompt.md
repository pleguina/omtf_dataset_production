# Sample Qualification: dy_prompt

## Dataset

- Role: Realistic prompt (different campaign)
- Dataset: /DYToLL_M-10To50_TuneCP5_14TeV-pythia8/Phase2Spring23DIGIRECOMiniAOD-PU200_Trk1GeV_131X_mcRun4_realistic_v5-v1/GEN-SIM-DIGI-RAW-MINIAOD
- Campaign: Phase2Spring23DIGIRECOMiniAOD
- CMSSW/GT inferred tag: 131X_mcRun4_realistic_v5

## One-file smoke input

- Representative file from DAS:
  - /store/mc/Phase2Spring23DIGIRECOMiniAOD/DYToLL_M-10To50_TuneCP5_14TeV-pythia8/GEN-SIM-DIGI-RAW-MINIAOD/PU200_Trk1GeV_131X_mcRun4_realistic_v5-v1/30000/5fe6b673-b9fc-4517-bc18-bf7270b11c88.root

## Qualification table

| Field | Value |
|---|---|
| dataset name | /DYToLL_M-10To50_TuneCP5_14TeV-pythia8/Phase2Spring23DIGIRECOMiniAOD-PU200_Trk1GeV_131X_mcRun4_realistic_v5-v1/GEN-SIM-DIGI-RAW-MINIAOD |
| campaign | Phase2Spring23DIGIRECOMiniAOD |
| CMSSW release / GT inferred | 131X_mcRun4_realistic_v5 |
| number of files processed | 1 (smoke target) |
| number of events (dataset summary) | 99402 |
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
