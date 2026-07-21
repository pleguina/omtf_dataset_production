/*
 * DataROOTDumper2AllInput.h
 *
 * Extension of DataROOTDumper2 that writes a second ROOT tree (OMTFAllInputTree)
 * containing ALL input stubs for each processor region per event — not only the
 * stubs matched by the best golden pattern.
 *
 * This is needed to train the Object Condensation and noise-rejection heads of the
 * HECIN+OC GNN, which require the full view of the detector hits in a region.
 *
 * The existing OMTFHitsTree (per-candidate stubs, from the base class) is still
 * written unchanged. Join the two trees on (reg_eventNum, reg_iProcessor,
 * reg_mtfType) to correlate regions with candidates.
 *
 * Author: pleguina (extended from DataROOTDumper2 by kbunkow)
 */

#ifndef L1T_OmtfP1_TOOLS_DATAROOTDUMPER2ALLINPUT_H_
#define L1T_OmtfP1_TOOLS_DATAROOTDUMPER2ALLINPUT_H_

#include "L1Trigger/L1TMuonOverlapPhase1/interface/Tools/DataROOTDumper2.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/OMTFinput.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Tools/EmulationObserverBase.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/AngleConverterBase.h"  // MuonGeometryTokens

#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/ESWatcher.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Utilities/interface/InputTag.h"

#include "Geometry/DTGeometry/interface/DTGeometry.h"
#include "Geometry/RPCGeometry/interface/RPCGeometry.h"
#include "Geometry/CSCGeometry/interface/CSCGeometry.h"
#include "Geometry/Records/interface/MuonGeometryRecord.h"

#include "DataFormats/MuonData/interface/MuonDigiCollection.h"
#include "DataFormats/Common/interface/DetSetVector.h"
#include "SimDataFormats/DigiSimLinks/interface/DTDigiSimLink.h"
#include "SimDataFormats/RPCDigiSimLink/interface/RPCDigiSimLink.h"
#include "SimDataFormats/TrackerDigiSimLink/interface/StripDigiSimLink.h"
#include "SimDataFormats/Track/interface/SimTrackContainer.h"
#include "SimDataFormats/TrackingHit/interface/PSimHitContainer.h"
#include "SimDataFormats/TrackingAnalysis/interface/TrackingParticle.h"
#include "DataFormats/HepMCCandidate/interface/GenParticle.h"
#include "DataFormats/HepMCCandidate/interface/GenParticleFwd.h"

#include <vector>
#include <map>
#include <set>
#include <memory>
#include <cstdint>

class TTree;

// ---------------------------------------------------------------------------
// DataROOTDumper2AllInput
// ---------------------------------------------------------------------------
// Adds a second ROOT tree (OMTFAllInputTree) on top of the existing OMTFHitsTree.
// Each entry of OMTFAllInputTree corresponds to one (event, processor-region) pair
// and holds ALL input stubs to that region (before pattern matching).
//
// Branch naming convention: reg_* for per-region scalars, reg_stub_* for the
// parallel stub vectors (same index → same stub).
//
// Two SimTrack-level truth branches are added when the digi-sim link
// InputTags are configured (i.e. doSimTruth == true):
//   reg_stub_trackId   : 0 = noise/unmatched, 1..K = gen-muon index (1-indexed)
//   reg_stub_ambiguous : 1 if the dominant SimTrack covers < 50 % of matched digis
// ---------------------------------------------------------------------------
class DataROOTDumper2AllInput : public DataROOTDumper2 {
public:
  DataROOTDumper2AllInput(const edm::ParameterSet& edmCfg,
                          const OMTFConfiguration* omtfConfig,
                          CandidateSimMuonMatcher* candidateSimMuonMatcher,
                          const MuonGeometryTokens& muonGeometryTokens);

  ~DataROOTDumper2AllInput() override;

  void beginRun(edm::EventSetup const& eventSetup) override;

  // Overrides the no-op in DataROOTDumper2: loops all layers/stubs in the
  // OMTFinput and caches them for writing in observeEventEnd.
  void observeProcesorEmulation(unsigned int iProcessor,
                                l1t::tftype mtfType,
                                const std::shared_ptr<OMTFinput>& input,
                                const AlgoMuons& algoCandidates,
                                const AlgoMuons& gbCandidates,
                                const FinalMuons& finalMuons) override;

  // Flushes the per-event region cache to OMTFAllInputTree, then calls the
  // base-class routine which writes OMTFHitsTree as before.
  void observeEventEnd(const edm::Event& iEvent,
                       std::unique_ptr<l1t::RegionalMuonCandBxCollection>& finalCandidates) override;

  void endJob() override;

private:
  void initializeAllInputTree();
  void initializeSchemaMetadataTree();

  // Assign reg_stub_trackId / reg_stub_ambiguous for one cached region.
  // Returns false (silently) when digi-sim link handles are not available.
  // simTrackIdToGenIdx / simTrackIdToTpIdx are keyed by the packed 64-bit key
  // (uint64_t(trackId) << 32 | eventId.rawId()), NOT by trackId alone: SimTrack
  // trackId numbering restarts per mixed sub-event, so a bare trackId->index
  // vector can silently alias a pileup SimTrack onto a hard-scatter gen-muon
  // index (or vice versa) when trackId values collide across sub-events at
  // high PU. Packing the EncodedEventId into the key removes that ambiguity.
  void assignSimTruth(
      const edm::Event& iEvent,
      const edm::Handle<MuonDigiCollection<DTLayerId, DTDigiSimLink>>& dtLinksH,
      const edm::Handle<edm::DetSetVector<RPCDigiSimLink>>& rpcLinksH,
      const edm::Handle<edm::DetSetVector<StripDigiSimLink>>& cscLinksH,
      const edm::Handle<edm::PSimHitContainer>& dtSimHitsH,
      const edm::Handle<edm::PSimHitContainer>& rpcSimHitsH,
      const edm::Handle<edm::PSimHitContainer>& cscSimHitsH,
      const std::map<uint64_t, int>& simTrackKeyToGenIdx,  // packed(trackId,eventId) → genMuon 1-indexed (0=none)
      const std::map<uint64_t, int>& simTrackKeyToTpIdx,   // packed(trackId,eventId) → TpMuon 1-indexed (0=none)
      unsigned int processorPhiZero,
      const std::vector<uint32_t>& stub_detId,
      const std::vector<short>& stub_phiHw,
      std::vector<signed char>& stub_trackId,
      std::vector<short>& stub_tpId,
      std::vector<uint8_t>& stub_ambiguous,
      std::vector<float>& stub_tof,
      std::vector<float>& stub_tofSpread,
      std::vector<signed char>& stub_nSimHit,
      std::vector<unsigned int>& stub_bestSimTrackId,
      std::vector<int>& stub_bestSimTrackEventId,
      std::vector<short>& stub_truthNContributors,
      std::vector<short>& stub_truthBestVotes,
      std::vector<short>& stub_truthSecondVotes,
      std::vector<float>& stub_truthBestFraction,
      std::vector<short>& stub_truthMargin,
      std::vector<signed char>& stub_noSelectedMuonReason,
      std::vector<signed char>& stub_dualIdRelation,
      std::set<uint64_t>& simTrackKeysWithStub);

  TTree* allInputTree = nullptr;
  TTree* schemaMetadataTree = nullptr;

  // --- schema/version metadata (see item 13 of the displaced-lane v5.2 plan) ---
  // Written once as a single-entry tree (OMTFSchemaMetadata) in endJob().
  // Bump these constants whenever the corresponding algorithm/definition changes.
  static constexpr int kProducerSchemaVersion = 2;     // overall OMTFAllInputTree/OMTFHitsTree schema
  static constexpr int kPrimitiveIdVersion = 2;        // 1 = old hash incl. processor-local input/phiHw (buggy); 2 = source-identity hash
  static constexpr int kTpSelectionVersion = 1;        // TpMuon selection: |pdgId|==13, pT>1.5 GeV, |eta|<1.6
  static constexpr int kTruthMatchingVersion = 2;      // 1 = trackId-only vote map; 2 = (trackId,eventId)-keyed vote map
  static constexpr int kPropagationVersion = 1;        // TpMuon_prop* : station-2 SteppingHelixPropagator proxy reused for St1
  static constexpr int kPileupProvenanceVersion = 1;   // reg_pu_src_* : no source-file identity yet (see item 4 limitations)
  static constexpr int kDtT0ConventionVersion = 0;     // 0 = unvalidated/unset (see Ph2DT*Digi_t0Valid docs)
  static constexpr const char* kEventIdConvention = "reg_eventNum64==iEvent.id().event() (exact 64-bit); reg_eventNum is a truncated 32-bit convenience field, NOT the primary key";

  // --- branches of OMTFAllInputTree ---
  unsigned int   reg_runNum     = 0;
  unsigned int   reg_lumiNum    = 0;
  unsigned long long reg_eventNum64 = 0;
  unsigned int   reg_eventNum   = 0;
  unsigned char  reg_iProcessor = 0;
  signed char    reg_mtfType    = 0;  // l1t::tftype cast to int8 (omtf_pos or omtf_neg)
  signed char    reg_endcap     = 0;  // +1 = OMTF_POS, -1 = OMTF_NEG, 0 = unknown

  std::vector<signed char> reg_stub_layer;    // logic layer index [0, nLayers)
  std::vector<short>       reg_stub_phiHw;    // absolute phi in HW units
  std::vector<short>       reg_stub_phiBHw;   // DT bending angle HW (0 for non-DT)
  std::vector<signed char> reg_stub_etaHw;    // eta HW (clamped to [-127, 127])
  std::vector<short>       reg_stub_r;        // radial distance [cm]
  std::vector<signed char> reg_stub_quality;  // qualityHw
  std::vector<signed char> reg_stub_type;     // MuonStub::Type cast to int8
  std::vector<signed char> reg_stub_bx;       // BX offset (clamped to [-127, 127])
  std::vector<uint32_t>    reg_stub_detId;    // stable source chamber/raw detId per stub
  std::vector<unsigned long long> reg_stub_primitiveId; // stable synthetic per-primitive ID (see kPrimitiveIdVersion)
  // Source-identity components used to build reg_stub_primitiveId (primitiveIdVersion=2).
  // Unlike reg_stub_phiHw (processor-local) these are invariant across processor duplication.
  std::vector<signed char>  reg_stub_sourceCollection; // == reg_stub_type (technology/subtype at source)
  std::vector<uint32_t>     reg_stub_sourceDetId;      // == reg_stub_detId (raw source chamber/roll DetId)
  std::vector<int>          reg_stub_sourceIndex;       // raw pre-conversion disambiguator, see MuonStub::sourceIndex
  std::vector<signed char>  reg_stub_sourceBx;          // == reg_stub_bx (source BX, pre-processor-window)
  // SimTrack truth (only filled when doSimTruth_ == true)
  std::vector<signed char> reg_stub_trackId;   // 0 = no hard-scatter GenMuon link, 1..K = gen-muon idx
  std::vector<short>       reg_stub_tpId;      // 0 = no selected TpMuon link, 1..M = TpMuon idx
  std::vector<uint8_t>     reg_stub_ambiguous; // 1 if dominant track < 50% digis
  std::vector<float>       reg_stub_tof;       // mean SimHit TOF [ns], -999 when unavailable
  std::vector<float>       reg_stub_tofSpread; // max-min SimHit TOF [ns], 0 when unavailable
  std::vector<signed char> reg_stub_nSimHit;   // number of SimHits used for TOF (clamped to 127)
  // Exact simulation identity of the winning (dominant-vote) SimTrack, independent of whether
  // it resolved to a GenMuon/TpMuon index. Sentinel 0 / -1 when no contributor exists.
  std::vector<unsigned int> reg_stub_bestSimTrackId;
  std::vector<int>          reg_stub_bestSimTrackEventId; // EncodedEventId::rawId(); 0 == hard-scatter convention
  // Truth-vote quality (item 8): truth-only, must never be used as a model input.
  std::vector<short>  reg_stub_truthNContributors; // number of distinct SimTrack contributors, 0 if none
  std::vector<short>  reg_stub_truthBestVotes;     // votes for the dominant contributor
  std::vector<short>  reg_stub_truthSecondVotes;   // votes for the runner-up contributor (0 if none)
  std::vector<float>  reg_stub_truthBestFraction;  // bestVotes/total; -1 sentinel when total==0
  std::vector<short>  reg_stub_truthMargin;        // bestVotes-secondVotes; -1 sentinel when total==0
  // Why this stub has no selected-muon link (trackId==0 && tpId==0). See enum in .cc.
  std::vector<signed char> reg_stub_noSelectedMuonReason;
  // Dual-ID relation for stubs with BOTH trackId>0 and tpId>0 (item 1). 0=n/a, 1=same physical
  // hard-scatter muon, 2=different physical muons, 3=ambiguous/unresolved (mask from hard association).
  std::vector<signed char> reg_stub_dualIdRelation;

  // Selected TrackingParticle muons persisted per event (duplicated for each region entry).
  std::vector<float>       TpMuon_pt;
  std::vector<float>       TpMuon_eta;
  std::vector<float>       TpMuon_phi;
  std::vector<signed char> TpMuon_charge;
  std::vector<float>       TpMuon_dxy;
  std::vector<float>       TpMuon_lxy;
  // Raw production-vertex transverse coordinates [cm]. Persisted alongside the
  // standard straight-line TpMuon_dxy (TrackingParticle::dxy()) so that a
  // curvature-consistent (helix-based) impact parameter can be reconstructed
  // downstream using TpMuon_pt/TpMuon_charge/TpMuon_phi and the known B field.
  // See DATASETS_INFO.txt "KNOWN CAVEAT: TpMuon_dxy is NOT the gun's true d0".
  std::vector<float>       TpMuon_vx;
  std::vector<float>       TpMuon_vy;
  std::vector<float>       TpMuon_vz;
  std::vector<float>       TpMuon_t;
  // Curvature-consistent (helix-based) impact parameter, computed the same way
  // as CandidateSimMuonMatcher::MatchingResult's SimTrack-path constructor
  // (rg from qinvpt/B, then d0 = rg + q*|center|), but applied to the
  // TrackingParticle-path muons, which that class still leaves on the naive
  // straight-line trackingParticle.dxy(). This is the value that should
  // actually stay within [MinDxy, MaxDxy] of the gun.
  std::vector<float>       TpMuon_dxyHelix;
  std::vector<int>         TpMuon_eventId;
  std::vector<signed char> TpMuon_bunchCrossing;
  std::vector<uint8_t>     TpMuon_isPileup;
  std::vector<uint8_t>     TpMuon_isInTime;
  std::vector<signed char> TpMuon_originClass;  // 0=HS, 1=PU-in-time, 2=PU-out-of-time, 3=other (BX origin ONLY, not ancestry)
  // Exact simulation identity (item 1): the first g4Track SimTrack backing this TrackingParticle.
  std::vector<unsigned int> TpMuon_simTrackId;
  std::vector<int>          TpMuon_simTrackEventId; // EncodedEventId::rawId()
  // Exact GenMuon <-> TpMuon relation (item 1). -1 = no correspondence, else 0-indexed GenMuon row.
  std::vector<int>          TpMuon_genMuonIdx;
  // 0=no correspondence, 1=unique exact correspondence, 2=ambiguous (>1 TpMuon maps to same GenMuon),
  // 3=conflicting (this TP's own g4Tracks map to different nonzero GenMuon indices).
  std::vector<signed char>  TpMuon_genMatchStatus;
  // Pileup ancestry (item 5). Same semantics as GenMuon_provenanceStatus where derivable (i.e. when
  // tp.genParticles() gives an exact ancestry chain); 3=unknown/unresolved otherwise (most out-of-time
  // / pure-PU TrackingParticles have no retained GenParticle ancestry in the Mixing module).
  std::vector<int>          TpMuon_parentPdgId;
  std::vector<signed char>  TpMuon_isDecayInFlight;
  std::vector<signed char>  TpMuon_provenanceStatus;
  // Pileup source-interaction relation (item 4): index into reg_pu_src_* for this TP's source
  // interaction, assuming the standard Mixing convention (eventId().event() is the 1-indexed
  // position of this sub-event within its BX in PileupSummaryInfo::getPU_EventID()). -1 = hard-scatter
  // sentinel or convention could not be validated for this row (see pileup provenance audit).
  std::vector<int>          TpMuon_puSourceIdx;
  // Propagated TP direction labels at MB1 (station 1) and MB2 (station 2).
  std::vector<float>       TpMuon_propEtaSt1;
  std::vector<float>       TpMuon_propPhiSt1;
  std::vector<float>       TpMuon_propEtaSt2;
  std::vector<float>       TpMuon_propPhiSt2;
  std::vector<uint8_t>     TpMuon_propOk;
  std::vector<uint8_t>     TpMuon_propInOmtfSt1;
  std::vector<uint8_t>     TpMuon_propInOmtfSt2;
  // Propagation failure/surface metadata (item 7). Station-1 currently reuses the station-2
  // propagation result as a conservative proxy (kPropagationVersion=1; see .cc for details), so
  // propSurfaceCodeSt1==propSurfaceCodeSt2 today. 0=success,1=invalid initial state,2=propagation
  // failed,3=did not intersect surface,4=numerical/geometry failure,5=outside supported region,
  // 6=matcher configured without a propagator (candidateSimMuonMatcherType=simpleMatching/
  // collectMuonCands -- no SteppingHelixPropagator ESProducer loaded in this workflow).
  std::vector<signed char> TpMuon_propFailureCode;
  std::vector<signed char> TpMuon_propSurfaceCodeSt1; // 0=unknown/proxy,1=MB-like,2=ME-like
  std::vector<signed char> TpMuon_propSurfaceCodeSt2;

  // Event-level TrackingParticle-muon truth coverage counters (item 9). Computed over the FULL
  // (unselected) TrackingParticle collection, not just the pT/eta-selected TpMuon_* table above.
  int nTpMuonAll = 0;
  int nTpMuonPtGt0p5 = 0;
  int nTpMuonPtGt1p5 = 0;
  int nTpMuonPtGt5 = 0;
  int nTpMuonInOmtfAcceptance = 0;              // |eta| in [0.7, 1.31] (OMTF acceptance proxy, any pT)
  int nTpMuonBelowSelectionWithOmtfStub = 0;    // below TpMuon_* selection but has >=1 matched stub this event

  // Pileup source-event provenance from addPileupInfo (if available).
  std::vector<signed char>         reg_pu_src_bx;
  std::vector<unsigned int>        reg_pu_src_run;
  std::vector<unsigned int>        reg_pu_src_lumi;
  std::vector<unsigned long long>  reg_pu_src_event;
  // reg_pu_src_hash: kept for backward compatibility, identical formula to reg_pu_src_occurrenceHash.
  std::vector<unsigned long long>  reg_pu_src_hash;
  // hash(run,lumi,event) only: identifies physical source-event reuse across different BX insertions.
  std::vector<unsigned long long>  reg_pu_src_eventHash;
  // hash(bx,run,lumi,event): identifies a particular insertion of that source event into a BX.
  std::vector<unsigned long long>  reg_pu_src_occurrenceHash;

  int reg_pu_nTpMuonInTime = 0;
  int reg_pu_nTpMuonOOT = 0;

  // --- per-event cache ---
  // Filled per-processor in observeProcesorEmulation, flushed in observeEventEnd.
  struct CachedRegion {
    unsigned char iProcessor = 0;
    signed char   mtfType    = 0;
    signed char   endcap     = 0;  // +1 = OMTF_POS, -1 = OMTF_NEG, 0 = unknown
    std::vector<signed char> stub_layer;
    std::vector<short>       stub_phiHw;
    std::vector<short>       stub_phiBHw;
    std::vector<signed char> stub_etaHw;
    std::vector<short>       stub_r;
    std::vector<signed char> stub_quality;
    std::vector<signed char> stub_type;
    std::vector<signed char> stub_bx;
    // cached for sim-truth assignment in observeEventEnd
    std::vector<uint32_t>    stub_detId;
    std::vector<unsigned long long> stub_primitiveId;
    std::vector<int>         stub_sourceIndex;  // raw MuonStub::sourceIndex, see primitiveId v2
  };

  std::vector<CachedRegion> cachedRegions;  // up to ~12 entries per event

  // Full (unselected) muon-type (|pdgId|==13) TrackingParticle lookup, keyed by packed
  // (g4Track trackId, eventId.rawId()). Rebuilt at the start of every event; used both for the
  // item-9 coverage counters and for reg_stub_noSelectedMuonReason classification.
  std::map<uint64_t, const TrackingParticle*> allMuonTpByKey_;

  // --- SimTrack truth infrastructure ---
  bool doSimTruth_ = false;

  const MuonGeometryTokens& muonGeometryTokens_;
  edm::ESWatcher<MuonGeometryRecord> geomWatcher_;
  edm::ESHandle<RPCGeometry> georpc_;
  edm::ESHandle<CSCGeometry> geocsc_;
  edm::ESHandle<DTGeometry>  geodt_;

  edm::InputTag dtSimLinkTag_;
  edm::InputTag rpcSimLinkTag_;
  edm::InputTag cscSimLinkTag_;
  edm::InputTag dtSimHitTag_;
  edm::InputTag rpcSimHitTag_;
  edm::InputTag cscSimHitTag_;
  edm::InputTag simTrackTag_;
  edm::InputTag genParticleTag_;
  edm::InputTag trackingParticleTag_;
  edm::InputTag pileupInfoTag_;
};

#endif /* L1T_OmtfP1_TOOLS_DATAROOTDUMPER2ALLINPUT_H_ */
