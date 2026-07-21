# Vendored CMSSW producer sources

This `src/` tree mirrors the CMSSW `src/` package layout and is installed into
a fresh CMSSW working area by [scripts/setup_cmssw.sh](../scripts/setup_cmssw.sh)
(see the "Install custom OMTF producers" step). It is the single source of
truth for the dumper/producer code — **do not edit the copies inside a live
`CMSSW_*/src` working area directly**; edit the files here and re-run
`setup_cmssw.sh` (or `scripts/install_producers.sh`) to propagate.

## Schema version

- **producerSchemaVersion = 2** (written to the `OMTFSchemaMetadata` TTree by
  `DataROOTDumper2AllInput`, one entry per job). See
  [scripts/ROOT_BRANCHES.md](../scripts/ROOT_BRANCHES.md) / the top-level
  `/afs/cern.ch/user/p/pleguina/ROOT_BRANCHES.md` "v5.2 schema additions"
  section for the full branch list and the meaning of every sub-version
  constant (`primitiveIdVersion`, `tpSelectionVersion`, `truthMatchingVersion`,
  `propagationVersion`, `pileupProvenanceVersion`, `dtT0ConventionVersion`).
- Origin CMSSW area: `CMSSW_14_2_0_pre2` / `el9_amd64_gcc12`, git branch
  `pelayo_dev` (local, as of 2026-07-21; not yet committed/pushed upstream).

## Normative dataset manifest

- Production/audit dataset authorization is now machine-readable in
  `../dataset_manifest.json` and should be treated as the normative source for:
  dataset role assignment, fragment/submit mapping, and split-policy constraints.
- `DATASETS_INFO.txt` remains the human-readable campaign narrative; in any
  mismatch, `dataset_manifest.json` is authoritative for audit scripts.

## Package inventory

### `L1Trigger/L1TMuonOverlapPhase1` — **forked package, vendored in full**

A package with this name ships in stock CMSSW, but the version used by this
analysis has diverged from it extensively: it is based on a separate OMTF
algorithm development fork (git remote `karol` →
`http://github.com/kbunkow/cmssw.git`) with further local changes on top
(git remote `my-cmssw` → `https://github.com/pleguina/cmssw.git`, branch
`pelayo_dev`). Of the package's 112 files, 40+ differ from the stock
`CMSSW_14_2_0_pre2` release, and several files (e.g. `interface/Omtf/FinalMuon.h`,
`interface/Omtf/XmlIOCache.h`, `interface/Omtf/DetailedDebugExporter.h`) don't
exist in stock CMSSW at all. **A diff-only overlay on top of a stock
`git cms-addpkg` checkout does NOT compile** (verified: missing types
`CscConversionInfo`, `OMTFinputMaker::DetectorInfo`, `FinalMuons` and an
`RpcDigiToStubsConverterOmtf` constructor signature mismatch). This package is
therefore vendored **in full** -- every file, not just the ones touched during
the v5.2 schema work.

The files most relevant to the v5.2 schema work (i.e. what actually changed
*this round*, for anyone diffing against an older vendored snapshot):

| File | Status | Purpose |
|---|---|---|
| `interface/MuonStub.h` | modified | Adds `sourceIndex` (raw, processor-invariant disambiguator) and `primitiveId` (precomputed stable per-primitive hash) fields |
| `interface/OmtfPrimitiveId.h` | **new** | Shared FNV-1a `primitiveId` hash formula (`primitiveIdVersion=2`), used by both `OMTFinputMaker.cc` (stub construction) and `DataROOTDumper2AllInput.cc` (ROOT dumping) so the formula is defined exactly once |
| `src/Omtf/OMTFinputMaker.cc` | modified | Populates `MuonStub::sourceIndex` in the DT/CSC/RPC stub builders and computes `MuonStub::primitiveId` once in `addStub()` |
| `interface/Tools/DataROOTDumper2AllInput.h` | modified | New source-identity/truth-vote-quality/TpMuon ancestry-provenance/pileup-provenance branches; `OMTFSchemaMetadata` tree; schema version constants |
| `src/Tools/DataROOTDumper2AllInput.cc` | modified | Implementation of all of the above; packed `(trackId,eventId)` truth-vote keys (fixes a PU trackId-collision bug); two-phase truth flush |
| `interface/Tools/DataROOTDumper2.h` | modified | Adds `omtfEndcap`/`omtfBx`/`candidateId` and `hits_primitiveId/layer/quality/etaHw/valid/detId` to `OMTFHitsTree` |
| `src/Tools/DataROOTDumper2.cc` | modified | Implementation of the above; reads `MuonStub::primitiveId` directly instead of recomputing the hash |

Everything else in the package (the base OMTF emulator: `OMTFProcessor`,
`GoldenPattern*`, `AlgoMuon`, `OMTFConfiguration`, plugins, python configs,
etc.) is carried over unchanged from the `pelayo_dev` branch and is required
for the package to compile/link at all.

### `L1Trigger/L1MuNano` — **fully custom package, vendored in full**

This package does **not** exist in stock CMSSW at all — it is entirely custom
to this repository.

| File | Status | Purpose |
|---|---|---|
| `BuildFile.xml` | unmodified | Package-level BuildFile |
| `plugins/BuildFile.xml` | modified | Added `SimDataFormats/TrackingAnalysis` + `hepmc` externals (required once `GenMuonProvenanceProducer.cc` consumes `TrackingParticle`) |
| `plugins/GenMuonProvenanceProducer.cc` | modified | Adds the optional `trackingParticles` input and the `tpMuonIdx` reciprocal `ValueMap<int>` (inverse of `TpMuon_genMuonIdx`) |
| `plugins/GenParticlePropagator.cc` | unmodified | Propagates gen particles to muon stations |
| `plugins/L1MuNanoPlugins.cc` | unmodified | `DEFINE_FWK_MODULE` plugin registration |
| `plugins/PileupInfoFlatTableProducer.cc` | unmodified | `Pileup_*` NanoAOD table |
| `plugins/PrimitiveDigiToFlatTableProducers.cc` | modified | Renamed `isLate` → `t0Valid` (DT phi + theta producers); doc corrected: `t0Valid` means "a timing measurement was assigned", NOT "the hit is late" |
| `python/__init__.py` | unmodified | Namespace package marker |
| `python/omtfNanoTables_cff.py` | modified | Wires `GenMuon_tpMuonIdx` `ExtVar` + `trackingParticles` tag on `genMuonProvenance` |

## Install mechanics

`scripts/install_producers.sh` copies both package directories **wholesale**
on top of `${CMSSW_BASE}/src/L1Trigger/...` — no `git cms-addpkg` / `git
cms-init` is needed or used. This has been verified end-to-end: a scratch
`CMSSW_14_2_0_pre2` project area (stock release, no prior L1Trigger checkout)
had both packages installed via `install_producers.sh` and then built cleanly
with `scram b -j 8 L1Trigger/L1TMuonOverlapPhase1 L1Trigger/L1MuNano`.

## Known limitations (carried over from the schema work, see ROOT_BRANCHES.md)

1. No HTCondor pilot job has been run against this exact vendored snapshot —
   only `scram b` compile-verified in the origin CMSSW area.
2. Pileup source-file identity (fileGuid/LFN) not yet included in provenance hashing.
3. Station-1 (MB1/ME1) propagation has NO independent implementation
   (`TpMuon_propFailureCodeSt1` is always `SURFACE_NOT_IMPLEMENTED`/7; `propOkSt1` is always 0;
   `propEtaSt1`/`propPhiSt1` are always NaN — never proxied from station-2 as of
   `propagationVersion=2`). Production propagation mode is frozen at
   `propagationMode=disabled_simpleMatching_v1` (see `customize_omtf_dumper.py`); switching to
   `steppingHelix_surfaceAware_v1` requires a small GEN,SIM,DIGI,L1 pilot first, verifying:
   no segfault; required EventSetup records available; nonzero propagation success; finite
   coordinates; independent St1/St2 results; valid surface codes; hard-scatter GenMuon/TpMuon
   propagation closure. Even after that pilot, St1 stays `SURFACE_NOT_IMPLEMENTED` until a
   dedicated MB1/ME1 propagation call is added to `DataROOTDumper2AllInput.cc` (station-1
   support does not come "for free" from enabling the propagator).
4. DT `t0Valid` has no calibrated in-time-vs-late threshold (`dtT0ConventionVersion=0`).
5. `TpMuon_puSourceIdx` convention (Mixing module indexing assumption) unvalidated.
