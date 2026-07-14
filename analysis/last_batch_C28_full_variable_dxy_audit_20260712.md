# Last Batch Audit: C28_mild_disp_pt5to10_overlap_PU200

Generated on 2026-07-12 from EOS outputs in:
- /eos/user/p/pleguina/omtf_hecin_datasets/prod/C28_mild_disp_pt5to10_overlap_PU200/

Condor context:
- Latest C-campaign generation cluster: 9297202
- Dataset: C28_mild_disp_pt5to10_overlap_PU200
- Jobs/files expected: 400

## Coverage

- Files analyzed: 400
- File index range: 0-399
- Events analyzed: 63,199
- Gen muons analyzed: 63,199
- File mtimes span: 2026-05-28 00:37:20 -> 2026-07-10 20:36:19

## Branch presence consistency

Present in all 400 files:
- GenMuon_pt, GenMuon_charge, GenMuon_phi, GenMuon_vx, GenMuon_vy
- GenMuon_dXY, GenMuon_lXY, GenMuon_eta, GenMuon_etaSt1, GenMuon_etaSt2
- GenMuon_phiSt1, GenMuon_phiSt2
- nomtf, omtf_hwQual, omtf_hwPt, omtf_hwDXY

Present in 373/400 files only:
- Pileup_nPU
- Pileup_nTrueInt

Files missing Pileup branches (27 files):
10, 115, 117, 119, 137, 140, 148, 19, 20, 200, 206, 211, 216, 235, 263, 289, 296, 365, 40, 43, 46, 48, 53, 57, 63, 70, 75

## Variable distributions (full dataset)

- GenMuon_pt: mean 6.9403, p50 6.6875, p95 9.5312, min 5.0, max 10.0
- GenMuon_dXY: mean -0.00036, p50 -0.04138, p95 0.19141, min -2.84375, max 2.92969
- GenMuon_lXY: mean 24.7705, p50 24.625, p95 47.5, min 0.06079, max 50.0
- GenMuon_eta: mean 0.00067, p50 0.82031, p95 1.17969, min -1.23828, max 1.23828
- Pileup_nPU (where present): mean 199.9597, p50 200, p95 223, min 145, max 256
- Pileup_nTrueInt (where present): constant 200
- nomtf: mean 1.1335, p50 1, p95 2, max 6
- omtf_hwQual: mean 10.715, p50 12, p95 12, max 12
- omtf_hwPt: mean 18.9762, p50 15, p95 25, p99 73, max 401
- omtf_hwDXY: all zero

## DXY consistency audit

Method:
- Recompute true helix-perigee d0 from stored GenMuon_pt, GenMuon_charge, GenMuon_phi, GenMuon_vx, GenMuon_vy.
- Compare stored GenMuon_dXY to recomputed d0.

Overall (all files):
- MAE(|delta dXY|): 0.08034 cm
- Median |delta dXY|: 0.01910 cm
- p95 |delta dXY|: 0.26369 cm
- p99 |delta dXY|: 1.55346 cm
- Max |delta dXY|: 3.12135 cm
- Fraction |delta dXY| > 0.1 cm: 0.11374
- Fraction |delta dXY| > 1 cm: 0.02144

Time-split diagnosis (by file mtime):
- First 100 files by mtime mean MAE: 0.23186 cm
- Last 100 files by mtime mean MAE: 0.02949 cm

Explicit old-vs-new split at 2026-07-10 12:00:
- Older files: 27
- Newer files: 373
- Older mean MAE: 0.77996 cm
- Newer mean MAE: 0.02941 cm
- Older files with MAE > 0.1 cm: 27/27
- Newer files with MAE > 0.1 cm: 0/373

## Conclusion

The last generated batch (C28 cluster 9297202) is mixed:
- 373 files are consistent with the newer/fixed dXY behavior.
- 27 files are consistent with older/pre-fix dXY behavior and also miss Pileup branches.

This is strong evidence of an in-between configuration change or partial overwrite during production/resubmission.

## Primary machine-readable artifact

- JSON details: /afs/cern.ch/user/p/pleguina/omtf_dataset_production/analysis/last_batch_C28_full_variable_dxy_audit_20260712.json
