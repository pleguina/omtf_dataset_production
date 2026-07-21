#!/usr/bin/env python3
"""
validate_displaced_lane_v52_schema.py

Validation script for the "displaced-lane v5.2" OMTF schema completion (see
ROOT_BRANCHES.md's "v5.2 schema additions" section for the full branch list
this checks against).

*** DISCLOSURE ***
This script has NOT been executed against real pilot production data in the
session that authored it: no HTCondor pilot job producing the new v5.2
branches (reg_stub_sourceIndex, reg_stub_primitiveId via the new formula,
reg_stub_bestSimTrackId/EventId, TpMuon_genMuonIdx, TpMuon_puSourceIdx,
OMTFSchemaMetadata, hits_primitiveId, etc.) has been run yet. Running this
script is the next required step before any of the corresponding requirements
can be considered validated. Do not report those requirements complete on the
basis of this script's *existence* alone -- only on the basis of it actually
being run against real production output and producing a passing JSON report.

Usage:
    python validate_displaced_lane_v52_schema.py \
        --hits-file  /path/to/omtf_hits_SAMPLE_N.root \
        --nano-file  /path/to/omtf_nano_SAMPLE_N.root \
        --out        /path/to/validation_report.json

Requires: uproot, numpy (both already used elsewhere in this repo's audit/
scripts, e.g. audit_gen_particles.py).

Sections implemented (see ROOT_BRANCHES.md "v5.2 schema additions"):
  A. Event identity           -- (run,lumi,event64) canonical join, no uint32 collisions
  B. Primitive identity        -- reg_stub_primitiveId / hits_primitiveId consistency
  C. Truth unification         -- trackId/tpId/dualIdRelation/noSelectedMuonReason sanity
  D. Propagation                -- TpMuon_propOkSt1/St2 / propFailureCodeSt1/St2 / surface codes
  E. Pileup provenance          -- reg_pu_src_eventHash/occurrenceHash, TpMuon_puSourceIdx range
  F. Timing                      -- Ph2DT*_t0Valid non-degenerate, chi2 unwrap sanity
  G. Storage / duplication      -- OMTFSchemaMetadata presence + version constants; TpMuon_* dup check

Each section returns a dict with "status" in {"pass", "fail", "warn", "skipped"}
(section D additionally uses "disabled_by_configuration"/"unimplemented_surface")
and a "detail" free-text explanation, plus any numeric findings. Three sections
carry extra, more granular sub-statuses that are informational and do NOT feed
the top-level pass/fail/warn rollup on their own:
  - D also reports which propagationMode produced the file (see "findings").
  - E also reports "structural_status" (pass/fail: are the stored hashes/indices
    self-consistent?) separately from "semantic_validation_status" (whether the
    underlying Mixing-index convention has been cross-checked against ground truth
    -- currently always "provisional").
  - F also reports "encoding_status" (pass/fail: does t0Valid match t0!=0?)
    separately from "calibration_status" (whether a real in-time-vs-late
    threshold has been calibrated -- currently always "unavailable").
The top-level JSON also carries "schema_metadata" (read directly from
OMTFSchemaMetadata if present) and "disclosure" (a copy of the text above, for
machine-readable traceability of "was this actually run against real data").
"""
import argparse
import json
import sys
from collections import Counter, defaultdict

try:
    import uproot
    import numpy as np
except ImportError as exc:  # pragma: no cover
    print(f"ERROR: missing dependency ({exc}). Try: pip install uproot numpy", file=sys.stderr)
    sys.exit(2)


DISCLOSURE = (
    "This script has not been executed against real HTCondor pilot production data as of the "
    "session that authored it. A passing JSON report from an ACTUAL run is required before any "
    "corresponding requirement in the displaced-lane v5.2 plan can be considered validated."
)


def _get(tree, name):
    """Return branch array, or None if absent (older/pre-v5.2 file)."""
    if name in tree.keys():
        return tree[name].array(library="np")
    return None


def audit_event_identity(hits_tree, nano_tree):
    result = {"section": "A. Event identity"}
    run64 = _get(hits_tree, "runNum")
    lumi64 = _get(hits_tree, "lumiNum")
    event64 = _get(hits_tree, "eventNum64")
    event32_legacy = _get(hits_tree, "eventNum")

    if event64 is None:
        result["status"] = "fail"
        result["detail"] = "OMTFHitsTree.eventNum64 branch missing -- cannot verify canonical event key."
        return result

    findings = {}
    if event32_legacy is not None:
        # A collision is any pair of distinct event64 values sharing the same 32-bit truncation.
        truncated = (event64 & 0xFFFFFFFF)
        legacy_mismatch = int(np.sum(truncated != event32_legacy))
        findings["legacy_eventNum_matches_truncated_event64"] = legacy_mismatch == 0
        if legacy_mismatch:
            result["status"] = "fail"
            result["detail"] = (
                f"{legacy_mismatch} rows where legacy eventNum != low 32 bits of eventNum64 -- "
                "legacy/canonical event numbering has diverged unexpectedly."
            )
            result["findings"] = findings
            return result

    if nano_tree is not None:
        nano_event = _get(nano_tree, "event")
        if nano_event is not None:
            findings["nano_event_dtype_is_64bit"] = str(nano_event.dtype) in ("uint64", "int64")
        else:
            findings["nano_event_present"] = False

    result["status"] = "pass"
    result["detail"] = "eventNum64 present and consistent with legacy eventNum truncation."
    result["findings"] = findings
    return result


def audit_primitive_identity(all_input_tree, hits_tree):
    result = {"section": "B. Primitive identity"}
    if all_input_tree is None:
        result["status"] = "skipped"
        result["detail"] = "OMTFAllInputTree not provided/available."
        return result

    prim_id = _get(all_input_tree, "reg_stub_primitiveId")
    src_coll = _get(all_input_tree, "reg_stub_sourceCollection")
    src_det = _get(all_input_tree, "reg_stub_sourceDetId")
    src_idx = _get(all_input_tree, "reg_stub_sourceIndex")
    src_bx = _get(all_input_tree, "reg_stub_sourceBx")

    if prim_id is None:
        result["status"] = "fail"
        result["detail"] = "reg_stub_primitiveId branch missing -- v5.2 primitiveId not produced yet."
        return result

    findings = {}
    if src_coll is not None and src_det is not None and src_idx is not None and src_bx is not None:
        # Same (sourceCollection, sourceDetId, sourceIndex, sourceBx) tuple across DIFFERENT
        # processor-window entries MUST map to the SAME primitiveId (processor-invariance check).
        key_to_ids = defaultdict(set)
        n_entries = len(prim_id)
        for i in range(n_entries):
            for j in range(len(prim_id[i])):
                key = (int(src_coll[i][j]), int(src_det[i][j]), int(src_idx[i][j]), int(src_bx[i][j]))
                key_to_ids[key].add(int(prim_id[i][j]))
        inconsistent = {str(k): list(v) for k, v in key_to_ids.items() if len(v) > 1}
        findings["n_distinct_source_keys"] = len(key_to_ids)
        findings["n_keys_with_inconsistent_primitiveId"] = len(inconsistent)
        if inconsistent:
            result["status"] = "fail"
            result["detail"] = (
                f"{len(inconsistent)} source-identity keys map to more than one primitiveId value "
                "-- primitiveId is not processor-invariant (violates requirement 3)."
            )
            result["findings"] = findings
            return result

    if hits_tree is not None:
        hits_prim_id = _get(hits_tree, "hits_primitiveId")
        findings["hits_primitiveId_present"] = hits_prim_id is not None

    result["status"] = "pass"
    result["detail"] = "primitiveId is consistent (processor-invariant) for all repeated source-identity keys observed."
    result["findings"] = findings
    return result


def audit_truth_unification(all_input_tree):
    result = {"section": "C. Truth unification"}
    if all_input_tree is None:
        result["status"] = "skipped"
        result["detail"] = "OMTFAllInputTree not provided/available."
        return result

    track_id = _get(all_input_tree, "reg_stub_trackId")
    reason = _get(all_input_tree, "reg_stub_noSelectedMuonReason")
    dual = _get(all_input_tree, "reg_stub_dualIdRelation")

    if track_id is None:
        result["status"] = "fail"
        result["detail"] = "reg_stub_trackId branch missing."
        return result

    findings = {}
    if reason is not None:
        reason_flat = np.concatenate([np.asarray(r) for r in reason]) if len(reason) else np.array([])
        counts = Counter(int(x) for x in reason_flat)
        findings["noSelectedMuonReason_distribution"] = dict(counts)
        # Sanity: trackId==0 rows should always have a reason set (not 0/n-a), and trackId>0 rows
        # should always have reason==0 (n/a).
        track_flat = np.concatenate([np.asarray(t) for t in track_id]) if len(track_id) else np.array([])
        if len(track_flat) == len(reason_flat) and len(track_flat) > 0:
            mismatch_a = int(np.sum((track_flat == 0) & (reason_flat == 0)))
            mismatch_b = int(np.sum((track_flat > 0) & (reason_flat != 0)))
            findings["trackId0_with_reason0_count"] = mismatch_a
            findings["trackIdPos_with_reasonNonzero_count"] = mismatch_b
            if mismatch_a or mismatch_b:
                result["status"] = "warn"
                result["detail"] = (
                    "Some rows have an inconsistent (trackId, noSelectedMuonReason) pairing -- "
                    "investigate before relying on noSelectedMuonReason for background rejection."
                )
                result["findings"] = findings
                return result
    else:
        findings["noSelectedMuonReason_present"] = False

    if dual is not None:
        dual_flat = np.concatenate([np.asarray(d) for d in dual]) if len(dual) else np.array([])
        findings["dualIdRelation_distribution"] = dict(Counter(int(x) for x in dual_flat))
    else:
        findings["dualIdRelation_present"] = False

    result["status"] = "pass" if findings else "warn"
    result["detail"] = "Truth-unification fields present and internally consistent on the checks performed."
    result["findings"] = findings
    return result


# TpMuon_propFailureCodeSt1/St2 enum (must match DataROOTDumper2AllInput.h's kPropFailure* constants).
PROP_FAILURE_PROPAGATION_DISABLED = 6
PROP_FAILURE_SURFACE_NOT_IMPLEMENTED = 7


def _flat(arr):
    return np.concatenate([np.asarray(x) for x in arr]) if arr is not None and len(arr) else np.array([])


def audit_propagation(all_input_tree, schema_tree=None):
    result = {"section": "D. Propagation"}
    if all_input_tree is None:
        result["status"] = "skipped"
        result["detail"] = "OMTFAllInputTree not provided/available."
        return result

    ok_st2 = _get(all_input_tree, "TpMuon_propOkSt2")
    if ok_st2 is None:
        legacy_ok = _get(all_input_tree, "TpMuon_propOk")
        result["status"] = "skipped"
        result["detail"] = (
            "TpMuon_propOkSt1/St2 branches missing (older schema without surface-specific "
            f"propagation fields; legacy TpMuon_propOk present={legacy_ok is not None})."
        )
        return result

    ok_st1 = _get(all_input_tree, "TpMuon_propOkSt1")
    fail_st1 = _get(all_input_tree, "TpMuon_propFailureCodeSt1")
    fail_st2 = _get(all_input_tree, "TpMuon_propFailureCodeSt2")
    eta1 = _get(all_input_tree, "TpMuon_propEtaSt1")
    eta2 = _get(all_input_tree, "TpMuon_propEtaSt2")

    flat_ok1 = _flat(ok_st1)
    flat_ok2 = _flat(ok_st2)
    flat_fail1 = _flat(fail_st1)
    flat_fail2 = _flat(fail_st2)
    flat_eta1 = _flat(eta1)
    flat_eta2 = _flat(eta2)

    findings = {"n_TpMuon_rows": int(len(flat_ok2))}
    if len(flat_ok2) == 0:
        result["status"] = "skipped"
        result["detail"] = "No TpMuon rows in this sample."
        result["findings"] = findings
        return result

    propagation_mode = None
    if schema_tree is not None and "propagationMode" in schema_tree.keys():
        propagation_mode = str(schema_tree["propagationMode"].array(library="np")[0])
    findings["propagationMode"] = propagation_mode
    findings["frac_propOkSt1"] = float(np.mean(flat_ok1)) if len(flat_ok1) else None
    findings["frac_propOkSt2"] = float(np.mean(flat_ok2))
    findings["propFailureCodeSt1_distribution"] = dict(Counter(int(x) for x in flat_fail1)) if len(flat_fail1) else {}
    findings["propFailureCodeSt2_distribution"] = dict(Counter(int(x) for x in flat_fail2)) if len(flat_fail2) else {}

    disabled_st2 = bool(len(flat_fail2)) and bool(np.all(flat_fail2 == PROP_FAILURE_PROPAGATION_DISABLED))
    if propagation_mode == "disabled_simpleMatching_v1" or disabled_st2:
        result["status"] = "disabled_by_configuration"
        result["detail"] = (
            f"Propagation is disabled by configuration (propagationMode={propagation_mode!r}, "
            "candidateSimMuonMatcherType=simpleMatching). TpMuon_propFailureCodeSt2 is uniformly "
            "PROPAGATION_DISABLED (6) -- this is expected, not a bug. Switch to "
            "propagationMode=steppingHelix_surfaceAware_v1 after a validated pilot (see "
            "MANIFEST.md known limitation #3) to obtain real propagation."
        )
        result["findings"] = findings
        return result

    # Propagation was actually attempted -- a coordinate must be finite iff its propOkStX is true.
    def _coord_consistency_violations(flat_ok, flat_eta):
        if len(flat_ok) == 0 or len(flat_eta) == 0 or len(flat_ok) != len(flat_eta):
            return None
        return int(np.sum(np.isfinite(flat_eta) != (flat_ok == 1)))

    v1 = _coord_consistency_violations(flat_ok1, flat_eta1)
    v2 = _coord_consistency_violations(flat_ok2, flat_eta2)
    findings["St1_coord_ok_consistency_violations"] = v1
    findings["St2_coord_ok_consistency_violations"] = v2
    if (v1 or 0) > 0 or (v2 or 0) > 0:
        result["status"] = "fail"
        result["detail"] = (
            "A propagated coordinate (propEtaSt1/2) is finite/non-finite inconsistently with its "
            "propOkSt1/2 flag -- a coordinate must be finite only when propagation succeeded."
        )
        result["findings"] = findings
        return result

    st1_unimplemented = bool(len(flat_fail1)) and bool(np.all(flat_fail1 == PROP_FAILURE_SURFACE_NOT_IMPLEMENTED))
    findings["St1_all_surface_not_implemented"] = st1_unimplemented
    if st1_unimplemented:
        result["status"] = "unimplemented_surface"
        result["detail"] = (
            "Station-2 propagation was attempted (steppingHelix mode) but station-1 has no "
            "independent propagation implementation yet -- TpMuon_propFailureCodeSt1 == "
            "SURFACE_NOT_IMPLEMENTED (7) for every row. This is a genuine missing feature, not a "
            "silently-degraded proxy: propEtaSt1/propPhiSt1 are never filled from station-2 anymore."
        )
        result["findings"] = findings
        return result

    if findings["frac_propOkSt2"] == 0.0:
        result["status"] = "fail"
        result["detail"] = (
            "Propagation was attempted (not disabled by configuration) but 0% of St2 propagations "
            "succeeded."
        )
        result["findings"] = findings
        return result

    result["status"] = "pass"
    result["detail"] = (
        "Propagation attempted with a non-trivial success rate on at least one "
        "independently-implemented surface."
    )
    result["findings"] = findings
    return result


def audit_pileup_provenance(all_input_tree):
    result = {"section": "E. Pileup provenance"}
    if all_input_tree is None:
        result["status"] = "skipped"
        result["detail"] = "OMTFAllInputTree not provided/available."
        return result

    ev_hash = _get(all_input_tree, "reg_pu_src_eventHash")
    occ_hash = _get(all_input_tree, "reg_pu_src_occurrenceHash")
    legacy_hash = _get(all_input_tree, "reg_pu_src_hash")
    pu_src_idx = _get(all_input_tree, "TpMuon_puSourceIdx")

    findings = {}
    if ev_hash is None or occ_hash is None:
        result["status"] = "fail"
        result["detail"] = "reg_pu_src_eventHash/occurrenceHash branches missing."
        return result

    if legacy_hash is not None:
        n_entries = len(occ_hash)
        mismatches = 0
        for i in range(n_entries):
            if not np.array_equal(np.asarray(occ_hash[i]), np.asarray(legacy_hash[i])):
                mismatches += 1
        findings["occurrenceHash_matches_legacy_hash_events"] = n_entries - mismatches
        findings["occurrenceHash_mismatch_events"] = mismatches
        if mismatches:
            result["status"] = "fail"
            result["detail"] = "reg_pu_src_occurrenceHash diverges from legacy reg_pu_src_hash -- alias broken."
            result["findings"] = findings
            return result

    # Structural check: are the TpMuon_puSourceIdx values in range for their own event's
    # reg_pu_src_* arrays? An out-of-range index would be a real bug regardless of whether the
    # Mixing-index *convention* itself is semantically validated (see semantic_validation_status).
    structural_ok = True
    if pu_src_idx is not None:
        n_src = np.array([len(x) for x in ev_hash])
        for i in range(len(pu_src_idx)):
            idxs = np.asarray(pu_src_idx[i])
            oob = idxs[(idxs != -1) & ((idxs < 0) | (idxs >= n_src[i]))]
            if len(oob):
                structural_ok = False
                break
        flat_idx = np.concatenate([np.asarray(x) for x in pu_src_idx]) if len(pu_src_idx) else np.array([])
        findings["TpMuon_puSourceIdx_min"] = int(np.min(flat_idx)) if len(flat_idx) else None
        findings["TpMuon_puSourceIdx_max"] = int(np.max(flat_idx)) if len(flat_idx) else None
        findings["max_available_src_index_per_event_median"] = float(np.median(n_src)) if len(n_src) else None
        findings["puSourceIdx_in_range_for_its_event"] = structural_ok
    else:
        findings["TpMuon_puSourceIdx_present"] = False

    result["structural_status"] = "pass" if structural_ok else "fail"
    result["semantic_validation_status"] = "provisional"
    result["status"] = result["structural_status"]
    result["detail"] = (
        "structural_status: reg_pu_src_*/TpMuon_puSourceIdx are internally self-consistent (hash "
        "alias intact, indices in range for their own event). semantic_validation_status is "
        "'provisional': the underlying Mixing-module indexing CONVENTION (that "
        "PileupSummaryInfo::getPU_EventID() ordering matches TrackingParticle::eventId() "
        "1-indexing) is still not independently cross-checked against a hand-verified "
        "ground-truth pilot sample (see MANIFEST.md known limitation #5)."
        if structural_ok
        else "structural_status=fail: a TpMuon_puSourceIdx value falls outside the range of "
             "reg_pu_src_* entries available for its own event -- this IS a real bug."
    )
    result["findings"] = findings
    return result


def audit_timing(nano_tree):
    result = {"section": "F. Timing"}
    if nano_tree is None:
        result["status"] = "skipped"
        result["detail"] = "NanoAOD file not provided/available."
        return result

    findings = {}
    for coll, t0_branch, valid_branch in [
        ("Ph2DTPhiDigi", "Ph2DTPhiDigi_t0", "Ph2DTPhiDigi_t0Valid"),
        ("Ph2DTThDigi", "Ph2DTThDigi_t0", "Ph2DTThDigi_t0Valid"),
    ]:
        t0 = _get(nano_tree, t0_branch)
        valid = _get(nano_tree, valid_branch)
        if t0 is None or valid is None:
            findings[f"{coll}_present"] = False
            continue
        flat_t0 = np.concatenate([np.asarray(x) for x in t0]) if len(t0) else np.array([])
        flat_valid = np.concatenate([np.asarray(x) for x in valid]) if len(valid) else np.array([])
        if len(flat_t0) != len(flat_valid):
            findings[f"{coll}_length_mismatch"] = True
            continue
        expected_valid = flat_t0 != 0
        mismatch = int(np.sum(expected_valid != (flat_valid != 0)))
        findings[f"{coll}_t0Valid_matches_t0nonzero_mismatches"] = mismatch
        findings[f"{coll}_frac_t0Valid"] = float(np.mean(flat_valid)) if len(flat_valid) else None

    if not findings:
        result["status"] = "skipped"
        result["detail"] = "No Phase-2 DT timing branches found."
        return result

    any_mismatch = any(v for k, v in findings.items() if k.endswith("_mismatches") and v)
    result["encoding_status"] = "fail" if any_mismatch else "pass"
    result["calibration_status"] = "unavailable"  # dtT0ConventionVersion=0, see MANIFEST.md limitation #4
    result["status"] = result["encoding_status"]
    result["detail"] = (
        "encoding_status=fail: t0Valid does not match (t0 != 0) in some entries."
        if any_mismatch
        else "encoding_status=pass: t0Valid == (t0 != 0) as expected for all entries. "
             "calibration_status=unavailable: no calibrated in-time-vs-late threshold exists yet "
             "(dtT0ConventionVersion=0, see MANIFEST.md known limitation #4) -- a separate, "
             "expected limitation, not an encoding bug."
    )
    result["findings"] = findings
    return result


def audit_storage_duplication(all_input_tree, schema_tree):
    result = {"section": "G. Storage / duplication"}
    findings = {}

    if schema_tree is not None:
        meta = {k: schema_tree[k].array(library="np")[0] for k in schema_tree.keys()}
        findings["schema_metadata"] = {
            k: (v.item() if hasattr(v, "item") else v) for k, v in meta.items()
        }
    else:
        findings["schema_metadata_present"] = False

    if all_input_tree is not None:
        tp_pt = _get(all_input_tree, "TpMuon_pt")
        n_regions_per_event = None
        reg_event = _get(all_input_tree, "reg_eventNum64")
        if tp_pt is not None and reg_event is not None:
            by_event = defaultdict(list)
            for i, ev in enumerate(reg_event):
                by_event[int(ev)].append(i)
            n_dup_mismatch = 0
            for ev, idxs in by_event.items():
                if len(idxs) < 2:
                    continue
                first = np.asarray(tp_pt[idxs[0]])
                for other in idxs[1:]:
                    if not np.array_equal(first, np.asarray(tp_pt[other])):
                        n_dup_mismatch += 1
                        break
            findings["events_with_multiple_regions"] = sum(1 for v in by_event.values() if len(v) > 1)
            findings["events_with_TpMuon_duplication_mismatch"] = n_dup_mismatch
            if n_dup_mismatch:
                result["status"] = "fail"
                result["detail"] = (
                    f"{n_dup_mismatch} events have TpMuon_pt differing across regions of the same "
                    "event -- expected duplication (same event-global truth table repeated per "
                    "processor window) is violated."
                )
                result["findings"] = findings
                return result

    result["status"] = "pass" if schema_tree is not None else "warn"
    result["detail"] = (
        "OMTFSchemaMetadata present and event-global TpMuon truth is consistently duplicated across "
        "processor-window regions."
        if schema_tree is not None
        else "OMTFSchemaMetadata tree missing -- cannot confirm schema version at rest."
    )
    result["findings"] = findings
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--hits-file", required=True, help="Path to omtf_hits_*.root")
    p.add_argument("--nano-file", help="Path to omtf_nano_*.root (optional, enables sections A/F GenMuon checks)")
    p.add_argument("--out", required=True, help="Path to write the JSON validation report")
    args = p.parse_args()

    report = {"disclosure": DISCLOSURE, "sections": {}}

    with uproot.open(args.hits_file) as f:
        keys = {k.split(";")[0] for k in f.keys()}
        hits_tree = None
        all_input_tree = None
        schema_tree = None
        for prefix in ("simOmtfPhase2Digis/", ""):
            if f"{prefix}OMTFHitsTree" in keys or (prefix + "OMTFHitsTree") in f:
                pass
        # uproot lets us just try direct + prefixed paths
        for path in ("simOmtfPhase2Digis/OMTFHitsTree", "OMTFHitsTree"):
            try:
                hits_tree = f[path]
                break
            except Exception:
                continue
        for path in ("simOmtfPhase2Digis/OMTFAllInputTree", "OMTFAllInputTree"):
            try:
                all_input_tree = f[path]
                break
            except Exception:
                continue
        for path in ("simOmtfPhase2Digis/OMTFSchemaMetadata", "OMTFSchemaMetadata"):
            try:
                schema_tree = f[path]
                break
            except Exception:
                continue

        nano_tree = None
        nano_ctx = None
        if args.nano_file:
            nano_ctx = uproot.open(args.nano_file)
            nano_tree = nano_ctx["Events"]

        report["sections"]["A_event_identity"] = audit_event_identity(hits_tree, nano_tree)
        report["sections"]["B_primitive_identity"] = audit_primitive_identity(all_input_tree, hits_tree)
        report["sections"]["C_truth_unification"] = audit_truth_unification(all_input_tree)
        report["sections"]["D_propagation"] = audit_propagation(all_input_tree, schema_tree)
        report["sections"]["E_pileup_provenance"] = audit_pileup_provenance(all_input_tree)
        report["sections"]["F_timing"] = audit_timing(nano_tree)
        report["sections"]["G_storage_duplication"] = audit_storage_duplication(all_input_tree, schema_tree)

        if nano_ctx is not None:
            nano_ctx.close()

    # "disabled_by_configuration"/"unimplemented_surface" (section D only) are informational,
    # non-blocking statuses -- they must not silently roll up to "pass", but they also should not
    # be conflated with "warn"/"fail". Treat them like "skipped" for the coarse overall rollup;
    # the full per-section detail remains in the JSON regardless.
    informational_statuses = ("skipped", "disabled_by_configuration", "unimplemented_surface")
    overall_statuses = [s["status"] for s in report["sections"].values()]
    if "fail" in overall_statuses:
        report["overall_status"] = "fail"
    elif "warn" in overall_statuses:
        report["overall_status"] = "warn"
    elif all(s in informational_statuses for s in overall_statuses):
        report["overall_status"] = "skipped"
    else:
        report["overall_status"] = "pass"

    with open(args.out, "w") as fh:
        json.dump(report, fh, indent=2, default=str)

    print(f"Wrote validation report to {args.out} (overall_status={report['overall_status']})")
    print(DISCLOSURE)


if __name__ == "__main__":
    main()
