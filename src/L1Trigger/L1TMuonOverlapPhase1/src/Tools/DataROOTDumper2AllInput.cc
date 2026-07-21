/*
 * DataROOTDumper2AllInput.cc
 *
 * See DataROOTDumper2AllInput.h for description.
 *
 * Author: pleguina
 */

#include "L1Trigger/L1TMuonOverlapPhase1/interface/Tools/DataROOTDumper2AllInput.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/OMTFinput.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/MuonStub.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/OMTFinputMaker.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/OMTFConfiguration.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/OmtfPrimitiveId.h"

#include "DataFormats/DetId/interface/DetId.h"
#include "DataFormats/MuonDetId/interface/MuonSubdetId.h"
#include "DataFormats/MuonDetId/interface/RPCDetId.h"
#include "DataFormats/MuonDetId/interface/DTChamberId.h"
#include "DataFormats/MuonDetId/interface/CSCDetId.h"

#include "Geometry/DTGeometry/interface/DTLayer.h"
#include "Geometry/DTGeometry/interface/DTChamber.h"
#include "Geometry/RPCGeometry/interface/RPCRoll.h"
#include "Geometry/CSCGeometry/interface/CSCLayer.h"
#include "Geometry/CSCGeometry/interface/CSCChamber.h"
#include "Geometry/CSCGeometry/interface/CSCLayerGeometry.h"

#include "CommonTools/UtilAlgos/interface/TFileService.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "SimDataFormats/PileupSummaryInfo/interface/PileupSummaryInfo.h"
#include "DataFormats/Candidate/interface/Candidate.h"

#include "TTree.h"
#include "TNamed.h"

#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace {
  inline uint64_t fnv1a64Update(uint64_t h, uint64_t x) {
    constexpr uint64_t kPrime = 1099511628211ULL;
    for (unsigned int i = 0; i < 8; ++i) {
      h ^= static_cast<uint8_t>((x >> (8 * i)) & 0xFFULL);
      h *= kPrime;
    }
    return h;
  }

  // reg_pu_src_eventHash: identifies physical source-event reuse (independent of which BX it
  // was mixed into). NOTE: does not yet include a source-file identity component (fileGuid/lfnHash)
  // -- see kPileupProvenanceVersion / ROOT_BRANCHES.md limitations. Collisions across DIFFERENT
  // MinBias input files that happen to share (run,lumi,event) numbering cannot be excluded yet.
  inline uint64_t makeSourceEventHash(uint32_t run, uint32_t lumi, uint64_t event) {
    uint64_t h = 1469598103934665603ULL;
    h = fnv1a64Update(h, static_cast<uint64_t>(run));
    h = fnv1a64Update(h, static_cast<uint64_t>(lumi));
    h = fnv1a64Update(h, event);
    return h;
  }

  // reg_pu_src_occurrenceHash (== legacy reg_pu_src_hash): identifies one particular insertion
  // of a source event into a BX of the mixed event.
  inline uint64_t makeSourceOccurrenceHash(int bx, uint32_t run, uint32_t lumi, uint64_t event) {
    uint64_t h = 1469598103934665603ULL;
    h = fnv1a64Update(h, static_cast<uint64_t>(static_cast<uint32_t>(bx)));
    h = fnv1a64Update(h, static_cast<uint64_t>(run));
    h = fnv1a64Update(h, static_cast<uint64_t>(lumi));
    h = fnv1a64Update(h, event);
    return h;
  }

  // Packs (SimTrack trackId, EncodedEventId rawId) into one 64-bit key. SimTrack trackId
  // numbering restarts per mixed sub-event, so trackId ALONE is not a globally unique key at
  // high PU -- see DataROOTDumper2AllInput.h assignSimTruth() doc (truthMatchingVersion=2).
  inline uint64_t packTrackKey(uint32_t trackId, uint32_t eventIdRawId) {
    return (static_cast<uint64_t>(trackId) << 32) | static_cast<uint64_t>(eventIdRawId);
  }
}


// ---------------------------------------------------------------------------
DataROOTDumper2AllInput::DataROOTDumper2AllInput(const edm::ParameterSet& edmCfg,
                                                 const OMTFConfiguration* omtfConfig,
                                                 CandidateSimMuonMatcher* candidateSimMuonMatcher,
                                                 const MuonGeometryTokens& muonGeometryTokens)
    : DataROOTDumper2(edmCfg, omtfConfig, candidateSimMuonMatcher),
      muonGeometryTokens_(muonGeometryTokens) {

    pileupInfoTag_ = edmCfg.exists("pileupInfoInputTag")
      ? edmCfg.getParameter<edm::InputTag>("pileupInfoInputTag")
      : edm::InputTag();

  // Sim-truth is optional: only enabled when the dtDigiSimLinksInputTag parameter exists
  if (edmCfg.exists("dtDigiSimLinksInputTag")) {
    doSimTruth_ = true;

    dtSimLinkTag_  = edmCfg.getParameter<edm::InputTag>("dtDigiSimLinksInputTag");
    rpcSimLinkTag_ = edmCfg.getParameter<edm::InputTag>("rpcDigiSimLinkInputTag");
    cscSimLinkTag_ = edmCfg.getParameter<edm::InputTag>("cscStripDigiSimLinksInputTag");
    dtSimHitTag_   = edmCfg.exists("dtSimHitsInputTag")
               ? edmCfg.getParameter<edm::InputTag>("dtSimHitsInputTag")
               : edm::InputTag("g4SimHits", "MuonDTHits");
    rpcSimHitTag_  = edmCfg.exists("rpcSimHitsInputTag")
               ? edmCfg.getParameter<edm::InputTag>("rpcSimHitsInputTag")
               : edm::InputTag("g4SimHits", "MuonRPCHits");
    cscSimHitTag_  = edmCfg.exists("cscSimHitsInputTag")
               ? edmCfg.getParameter<edm::InputTag>("cscSimHitsInputTag")
               : edm::InputTag("g4SimHits", "MuonCSCHits");
    simTrackTag_   = edmCfg.getParameter<edm::InputTag>("simTracksTag");
    genParticleTag_= edmCfg.getParameter<edm::InputTag>("genParticleTag");
    if (edmCfg.exists("trackingParticleTag"))
      trackingParticleTag_ = edmCfg.getParameter<edm::InputTag>("trackingParticleTag");

    edm::LogVerbatim("l1tOmtfEventPrint") << "DataROOTDumper2AllInput: doSimTruth = true" << std::endl;
  }

  initializeAllInputTree();
  edm::LogVerbatim("l1tOmtfEventPrint") << "DataROOTDumper2AllInput created" << std::endl;
}

DataROOTDumper2AllInput::~DataROOTDumper2AllInput() {}

// ---------------------------------------------------------------------------
void DataROOTDumper2AllInput::beginRun(edm::EventSetup const& eventSetup) {
  DataROOTDumper2::beginRun(eventSetup);

  if (doSimTruth_ && geomWatcher_.check(eventSetup)) {
    georpc_ = eventSetup.getHandle(muonGeometryTokens_.rpcGeometryEsToken);
    geocsc_ = eventSetup.getHandle(muonGeometryTokens_.cscGeometryEsToken);
    geodt_  = eventSetup.getHandle(muonGeometryTokens_.dtGeometryEsToken);
  }
}

// ---------------------------------------------------------------------------
void DataROOTDumper2AllInput::initializeAllInputTree() {
  edm::Service<TFileService> fs;

  allInputTree =
      fs->make<TTree>("OMTFAllInputTree", "All input stubs per processor region per event");

  allInputTree->Branch("reg_runNum",      &reg_runNum,    "reg_runNum/i");
  allInputTree->Branch("reg_lumiNum",     &reg_lumiNum,   "reg_lumiNum/i");
  allInputTree->Branch("reg_eventNum64",  &reg_eventNum64, "reg_eventNum64/L");
  allInputTree->Branch("reg_eventNum",    &reg_eventNum,  "reg_eventNum/i");
  allInputTree->Branch("reg_iProcessor",  &reg_iProcessor, "reg_iProcessor/b");
  allInputTree->Branch("reg_mtfType",     &reg_mtfType, "reg_mtfType/B");
  allInputTree->Branch("reg_endcap",      &reg_endcap,  "reg_endcap/B");

  allInputTree->Branch("reg_stub_layer",   &reg_stub_layer);
  allInputTree->Branch("reg_stub_phiHw",   &reg_stub_phiHw);
  allInputTree->Branch("reg_stub_phiBHw",  &reg_stub_phiBHw);
  allInputTree->Branch("reg_stub_etaHw",   &reg_stub_etaHw);
  allInputTree->Branch("reg_stub_r",       &reg_stub_r);
  allInputTree->Branch("reg_stub_quality", &reg_stub_quality);
  allInputTree->Branch("reg_stub_type",    &reg_stub_type);
  allInputTree->Branch("reg_stub_bx",      &reg_stub_bx);
  allInputTree->Branch("reg_stub_detId",   &reg_stub_detId);
  allInputTree->Branch("reg_stub_primitiveId", &reg_stub_primitiveId);
  allInputTree->Branch("reg_stub_sourceCollection", &reg_stub_sourceCollection);
  allInputTree->Branch("reg_stub_sourceDetId",      &reg_stub_sourceDetId);
  allInputTree->Branch("reg_stub_sourceIndex",      &reg_stub_sourceIndex);
  allInputTree->Branch("reg_stub_sourceBx",         &reg_stub_sourceBx);

  // SimTrack truth branches are always created so the tree schema is stable.
  // When doSimTruth_ == false they stay filled with zeros.
  allInputTree->Branch("reg_stub_trackId",   &reg_stub_trackId);
  allInputTree->Branch("reg_stub_tpId",      &reg_stub_tpId);
  allInputTree->Branch("reg_stub_ambiguous", &reg_stub_ambiguous);
  allInputTree->Branch("reg_stub_tof",       &reg_stub_tof);
  allInputTree->Branch("reg_stub_tofSpread", &reg_stub_tofSpread);
  allInputTree->Branch("reg_stub_nSimHit",   &reg_stub_nSimHit);
  allInputTree->Branch("reg_stub_bestSimTrackId",      &reg_stub_bestSimTrackId);
  allInputTree->Branch("reg_stub_bestSimTrackEventId", &reg_stub_bestSimTrackEventId);
  allInputTree->Branch("reg_stub_truthNContributors",  &reg_stub_truthNContributors);
  allInputTree->Branch("reg_stub_truthBestVotes",      &reg_stub_truthBestVotes);
  allInputTree->Branch("reg_stub_truthSecondVotes",    &reg_stub_truthSecondVotes);
  allInputTree->Branch("reg_stub_truthBestFraction",   &reg_stub_truthBestFraction);
  allInputTree->Branch("reg_stub_truthMargin",         &reg_stub_truthMargin);
  allInputTree->Branch("reg_stub_noSelectedMuonReason", &reg_stub_noSelectedMuonReason);
  allInputTree->Branch("reg_stub_dualIdRelation",       &reg_stub_dualIdRelation);

  allInputTree->Branch("TpMuon_pt",            &TpMuon_pt);
  allInputTree->Branch("TpMuon_eta",           &TpMuon_eta);
  allInputTree->Branch("TpMuon_phi",           &TpMuon_phi);
  allInputTree->Branch("TpMuon_charge",        &TpMuon_charge);
  allInputTree->Branch("TpMuon_dxy",           &TpMuon_dxy);
  allInputTree->Branch("TpMuon_lxy",           &TpMuon_lxy);
  allInputTree->Branch("TpMuon_vx",            &TpMuon_vx);
  allInputTree->Branch("TpMuon_vy",            &TpMuon_vy);
  allInputTree->Branch("TpMuon_vz",            &TpMuon_vz);
  allInputTree->Branch("TpMuon_t",             &TpMuon_t);
  allInputTree->Branch("TpMuon_dxyHelix",      &TpMuon_dxyHelix);
  allInputTree->Branch("TpMuon_eventId",       &TpMuon_eventId);
  allInputTree->Branch("TpMuon_bunchCrossing", &TpMuon_bunchCrossing);
  allInputTree->Branch("TpMuon_isPileup",      &TpMuon_isPileup);
  allInputTree->Branch("TpMuon_isInTime",      &TpMuon_isInTime);
  allInputTree->Branch("TpMuon_originClass",   &TpMuon_originClass);
  allInputTree->Branch("TpMuon_simTrackId",      &TpMuon_simTrackId);
  allInputTree->Branch("TpMuon_simTrackEventId", &TpMuon_simTrackEventId);
  allInputTree->Branch("TpMuon_genMuonIdx",      &TpMuon_genMuonIdx);
  allInputTree->Branch("TpMuon_genMatchStatus",  &TpMuon_genMatchStatus);
  allInputTree->Branch("TpMuon_parentPdgId",       &TpMuon_parentPdgId);
  allInputTree->Branch("TpMuon_isDecayInFlight",   &TpMuon_isDecayInFlight);
  allInputTree->Branch("TpMuon_provenanceStatus",  &TpMuon_provenanceStatus);
  allInputTree->Branch("TpMuon_puSourceIdx",       &TpMuon_puSourceIdx);
  allInputTree->Branch("TpMuon_propEtaSt1",    &TpMuon_propEtaSt1);
  allInputTree->Branch("TpMuon_propPhiSt1",    &TpMuon_propPhiSt1);
  allInputTree->Branch("TpMuon_propEtaSt2",    &TpMuon_propEtaSt2);
  allInputTree->Branch("TpMuon_propPhiSt2",    &TpMuon_propPhiSt2);
  allInputTree->Branch("TpMuon_propOk",        &TpMuon_propOk);
  allInputTree->Branch("TpMuon_propInOmtfSt1", &TpMuon_propInOmtfSt1);
  allInputTree->Branch("TpMuon_propInOmtfSt2", &TpMuon_propInOmtfSt2);
  allInputTree->Branch("TpMuon_propFailureCode",     &TpMuon_propFailureCode);
  allInputTree->Branch("TpMuon_propSurfaceCodeSt1",  &TpMuon_propSurfaceCodeSt1);
  allInputTree->Branch("TpMuon_propSurfaceCodeSt2",  &TpMuon_propSurfaceCodeSt2);

  allInputTree->Branch("nTpMuonAll",             &nTpMuonAll,             "nTpMuonAll/I");
  allInputTree->Branch("nTpMuonPtGt0p5",         &nTpMuonPtGt0p5,         "nTpMuonPtGt0p5/I");
  allInputTree->Branch("nTpMuonPtGt1p5",         &nTpMuonPtGt1p5,         "nTpMuonPtGt1p5/I");
  allInputTree->Branch("nTpMuonPtGt5",           &nTpMuonPtGt5,           "nTpMuonPtGt5/I");
  allInputTree->Branch("nTpMuonInOmtfAcceptance", &nTpMuonInOmtfAcceptance, "nTpMuonInOmtfAcceptance/I");
  allInputTree->Branch("nTpMuonBelowSelectionWithOmtfStub", &nTpMuonBelowSelectionWithOmtfStub,
                       "nTpMuonBelowSelectionWithOmtfStub/I");

  allInputTree->Branch("reg_pu_src_bx",        &reg_pu_src_bx);
  allInputTree->Branch("reg_pu_src_run",       &reg_pu_src_run);
  allInputTree->Branch("reg_pu_src_lumi",      &reg_pu_src_lumi);
  allInputTree->Branch("reg_pu_src_event",     &reg_pu_src_event);
  allInputTree->Branch("reg_pu_src_hash",      &reg_pu_src_hash);
  allInputTree->Branch("reg_pu_src_eventHash",      &reg_pu_src_eventHash);
  allInputTree->Branch("reg_pu_src_occurrenceHash", &reg_pu_src_occurrenceHash);

  allInputTree->Branch("reg_pu_nTpMuonInTime", &reg_pu_nTpMuonInTime, "reg_pu_nTpMuonInTime/I");
  allInputTree->Branch("reg_pu_nTpMuonOOT",    &reg_pu_nTpMuonOOT,    "reg_pu_nTpMuonOOT/I");

  initializeSchemaMetadataTree();
}

// ---------------------------------------------------------------------------
// OMTFSchemaMetadata: single-entry tree recording the producer/schema version
// constants defined in the header (item 13 of the displaced-lane v5.2 plan).
// Downstream consumers should read this tree FIRST and fail closed on an
// eventIdConvention / primitiveIdVersion / truthMatchingVersion they do not
// recognize, rather than silently assuming the legacy (v1) semantics.
// ---------------------------------------------------------------------------
void DataROOTDumper2AllInput::initializeSchemaMetadataTree() {
  edm::Service<TFileService> fs;
  schemaMetadataTree = fs->make<TTree>("OMTFSchemaMetadata", "Producer schema/version metadata (one entry)");

  int producerSchemaVersion = kProducerSchemaVersion;
  int primitiveIdVersion = kPrimitiveIdVersion;
  int tpSelectionVersion = kTpSelectionVersion;
  int truthMatchingVersion = kTruthMatchingVersion;
  int propagationVersion = kPropagationVersion;
  int pileupProvenanceVersion = kPileupProvenanceVersion;
  int dtT0ConventionVersion = kDtT0ConventionVersion;
  std::string eventIdConvention = kEventIdConvention;
  std::string producerGitCommit = "UNSET_fill_from_production_manifest";  // see item 13 commands

  schemaMetadataTree->Branch("producerSchemaVersion", &producerSchemaVersion, "producerSchemaVersion/I");
  schemaMetadataTree->Branch("primitiveIdVersion", &primitiveIdVersion, "primitiveIdVersion/I");
  schemaMetadataTree->Branch("tpSelectionVersion", &tpSelectionVersion, "tpSelectionVersion/I");
  schemaMetadataTree->Branch("truthMatchingVersion", &truthMatchingVersion, "truthMatchingVersion/I");
  schemaMetadataTree->Branch("propagationVersion", &propagationVersion, "propagationVersion/I");
  schemaMetadataTree->Branch("pileupProvenanceVersion", &pileupProvenanceVersion, "pileupProvenanceVersion/I");
  schemaMetadataTree->Branch("dtT0ConventionVersion", &dtT0ConventionVersion, "dtT0ConventionVersion/I");
  schemaMetadataTree->Branch("eventIdConvention", &eventIdConvention);
  schemaMetadataTree->Branch("producerGitCommit", &producerGitCommit);
  schemaMetadataTree->Fill();
}

// ---------------------------------------------------------------------------
void DataROOTDumper2AllInput::observeProcesorEmulation(unsigned int iProcessor,
                                                       l1t::tftype mtfType,
                                                       const std::shared_ptr<OMTFinput>& omtfInput,
                                                       const AlgoMuons& /*algoCandidates*/,
                                                       const AlgoMuons& /*gbCandidates*/,
                                                       const FinalMuons& /*finalMuons*/) {
  CachedRegion region;
  region.iProcessor = static_cast<unsigned char>(iProcessor);
  region.mtfType    = static_cast<signed char>(mtfType);
  region.endcap     = (mtfType == l1t::omtf_neg) ? -1 : ((mtfType == l1t::omtf_pos) ? 1 : 0);

  // getMuonStubs() returns MuonStubPtrs2D = vector<vector<MuonStubPtr>>
  // indexed [iLayer][iStub]. Iterate all layers and all stubs.
  const auto& muonStubsInLayers = omtfInput->getMuonStubs();
  for (unsigned int iLayer = 0; iLayer < muonStubsInLayers.size(); ++iLayer) {
    for (const auto& stub : muonStubsInLayers[iLayer]) {
      if (!stub)
        continue;

      region.stub_layer.push_back(static_cast<signed char>(iLayer));
      region.stub_phiHw.push_back(static_cast<short>(stub->phiHw));
      region.stub_phiBHw.push_back(static_cast<short>(stub->phiBHw));

      int etaHw = stub->etaHw;
      if (etaHw > 127)
        etaHw = 127;
      else if (etaHw < -127)
        etaHw = -127;
      region.stub_etaHw.push_back(static_cast<signed char>(etaHw));

      region.stub_r.push_back(static_cast<short>(stub->r));
      region.stub_quality.push_back(static_cast<signed char>(stub->qualityHw));
      region.stub_type.push_back(static_cast<signed char>(stub->type));

      int bx = stub->bx;
      if (bx > 127)
        bx = 127;
      else if (bx < -127)
        bx = -127;
      region.stub_bx.push_back(static_cast<signed char>(bx));

      // Store raw DetId for sim-truth lookup in observeEventEnd
      region.stub_detId.push_back(static_cast<uint32_t>(stub->detId));
      region.stub_sourceIndex.push_back(stub->sourceIndex);
      // primitiveId is now computed once at stub-construction time in
      // OMTFinputMaker::addStub() (see MuonStub::primitiveId); read it directly here
      // instead of recomputing, so DataROOTDumper2.cc (per-candidate hits) and
      // OMTFAllInputTree (all-input stubs) are guaranteed to agree on the same value
      // for the same physical primitive.
      region.stub_primitiveId.push_back(stub->primitiveId);
    }
  }

  // Only store regions that actually had at least one stub
  if (!region.stub_layer.empty())
    cachedRegions.push_back(std::move(region));
}

// ---------------------------------------------------------------------------
// Helper: match one stub to SimTracks via digi-sim links.
// Returns the dominant SimTrack ID (or 0) and sets ambiguous.
// ---------------------------------------------------------------------------
namespace {

  // Build a map  simTrackId → count of matched digis  for one stub
  // using the three digi-sim link collections.
  void accumRPC(uint32_t detIdRaw,
                double stubGlobalPhi,
                const edm::DetSetVector<RPCDigiSimLink>& rpcLinks,
                const RPCGeometry& georpc,
                std::map<uint64_t, unsigned>& votes) {
    RPCDetId rpcDetId(detIdRaw);
    const RPCRoll* roll = georpc.roll(rpcDetId);
    if (!roll)
      return;
    const int nstripsRpc = roll->nstrips();
    // Use equal_range instead of find() to avoid assertion on duplicate DetSets
    auto rpcRange = std::equal_range(rpcLinks.begin(), rpcLinks.end(), edm::det_id_type(detIdRaw));
    for (auto dsIt = rpcRange.first; dsIt != rpcRange.second; ++dsIt) {
      for (const auto& link : *dsIt) {
        int strip = static_cast<int>(link.getStrip());
        if (strip < 1 || strip > nstripsRpc)
          continue;
        double phi = (roll->toGlobal(roll->centreOfStrip(strip))).phi();
        if (std::abs(stubGlobalPhi - phi) < 0.02)
          votes[packTrackKey(link.getTrackId(), link.getEventId().rawId())]++;
      }
    }
  }

  void accumDT(uint32_t detIdRaw,
               double stubGlobalPhi,
               const MuonDigiCollection<DTLayerId, DTDigiSimLink>& dtLinks,
               const DTGeometry& geodt,
               std::map<uint64_t, unsigned>& votes) {
    const DTChamber* chamber = geodt.chamber(DTLayerId(detIdRaw));
    if (!chamber)
      return;
    for (auto* superlayer : chamber->superLayers()) {
      if (superlayer->id().superLayer() == 2)  // skip theta
        continue;
      for (auto* layer : superlayer->layers()) {
        const int firstWire = layer->specificTopology().firstChannel();
        const int lastWire  = layer->specificTopology().lastChannel();
        auto range = dtLinks.get(layer->id());
        for (auto lnk = range.first; lnk != range.second; ++lnk) {
          int wire = static_cast<int>(lnk->wire());
          if (wire < firstWire || wire > lastWire)
            continue;
          auto wireX   = layer->specificTopology().wirePosition(wire);
          auto digiPhi = layer->toGlobal(LocalPoint(wireX, 0, 0)).phi();
          if (std::abs(stubGlobalPhi - digiPhi) < 0.03)
            votes[packTrackKey(lnk->SimTrackId(), lnk->eventId().rawId())]++;
        }
      }
    }
  }

  void accumCSC(uint32_t detIdRaw,
                double stubGlobalPhi,
                const edm::DetSetVector<StripDigiSimLink>& cscLinks,
                const CSCGeometry& geocsc,
                std::map<uint64_t, unsigned>& votes) {
    const CSCChamber* chamber = geocsc.chamber(CSCDetId(detIdRaw));
    if (!chamber)
      return;
    for (const auto* layer : chamber->layers()) {
      // Use equal_range instead of find() to avoid assertion on duplicate DetSets
      auto cscRange = std::equal_range(
          cscLinks.begin(), cscLinks.end(), edm::det_id_type(layer->id().rawId()));
      const int nstrips = layer->geometry()->numberOfStrips();
      for (auto dsIt = cscRange.first; dsIt != cscRange.second; ++dsIt) {
        for (const auto& link : *dsIt) {
          int strip = static_cast<int>(link.channel());
          if (strip < 1 || strip > nstrips)
            continue;
          auto phi = layer->centerOfStrip(strip).phi();
          if (std::abs(stubGlobalPhi - phi) < 0.03)
            votes[packTrackKey(link.SimTrackId(), link.eventId().rawId())]++;
        }
      }
    }
  }

  void collectRPCTof(uint32_t detIdRaw,
                     double stubGlobalPhi,
                     const edm::PSimHitContainer& rpcSimHits,
                     const RPCGeometry& georpc,
                     std::vector<float>& tofs) {
    RPCDetId rpcDetId(detIdRaw);
    const RPCRoll* roll = georpc.roll(rpcDetId);
    if (!roll)
      return;
    for (const auto& simHit : rpcSimHits) {
      if (simHit.detUnitId() != detIdRaw)
        continue;
      const int strip = roll->strip(simHit.localPosition());
      if (strip < 1 || strip > roll->nstrips())
        continue;
      const double phi = roll->toGlobal(roll->centreOfStrip(strip)).phi();
      if (std::abs(stubGlobalPhi - phi) < 0.02)
        tofs.push_back(simHit.timeOfFlight());
    }
  }

  void collectDTTof(uint32_t detIdRaw,
                    double stubGlobalPhi,
                    const edm::PSimHitContainer& dtSimHits,
                    const DTGeometry& geodt,
                    std::vector<float>& tofs) {
    const DTChamber* chamber = geodt.chamber(DTLayerId(detIdRaw));
    if (!chamber)
      return;
    for (const auto& simHit : dtSimHits) {
      const DTLayer* layer = geodt.layer(DTLayerId(simHit.detUnitId()));
      if (!layer)
        continue;
      if (layer->chamber()->id().rawId() != detIdRaw)
        continue;
      const auto globalPoint = layer->toGlobal(simHit.localPosition());
      if (std::abs(stubGlobalPhi - globalPoint.phi()) < 0.03)
        tofs.push_back(simHit.timeOfFlight());
    }
  }

  void collectCSCTof(uint32_t detIdRaw,
                     double stubGlobalPhi,
                     const edm::PSimHitContainer& cscSimHits,
                     const CSCGeometry& geocsc,
                     std::vector<float>& tofs) {
    const CSCChamber* chamber = geocsc.chamber(CSCDetId(detIdRaw));
    if (!chamber)
      return;
    for (const auto& simHit : cscSimHits) {
      const CSCLayer* layer = geocsc.layer(CSCDetId(simHit.detUnitId()));
      if (!layer)
        continue;
      if (layer->chamber()->id().rawId() != detIdRaw)
        continue;
      const double strip = layer->geometry()->strip(simHit.localPosition());
      const double phi = layer->centerOfStrip(std::round(strip)).phi().value();
      if (std::abs(stubGlobalPhi - phi) < 0.03)
        tofs.push_back(simHit.timeOfFlight());
    }
  }

  void summarizeTof(const std::vector<float>& tofs, float& meanTof, float& spreadTof, signed char& nTof) {
    if (tofs.empty()) {
      meanTof = -999.f;
      spreadTof = 0.f;
      nTof = 0;
      return;
    }

    float sum = 0.f;
    float minTof = std::numeric_limits<float>::max();
    float maxTof = std::numeric_limits<float>::lowest();
    for (float t : tofs) {
      sum += t;
      if (t < minTof) minTof = t;
      if (t > maxTof) maxTof = t;
    }
    meanTof = sum / static_cast<float>(tofs.size());
    spreadTof = maxTof - minTof;
    const size_t capped = std::min<size_t>(tofs.size(), 127);
    nTof = static_cast<signed char>(capped);
  }

}  // anonymous namespace

namespace {
  // Mirrors L1Trigger/L1MuNano/plugins/GenMuonProvenanceProducer.cc's classification exactly
  // (kept in sync manually; a future refactor could share a common header). Applied here to the
  // GenParticle pointed to by TrackingParticle::genParticles() (when available) to fill the
  // TpMuon_parentPdgId/isDecayInFlight/provenanceStatus ancestry fields (item 5).
  bool isDifParentPdgIdLocal(int pdgIdAbs) {
    return pdgIdAbs == 211 || pdgIdAbs == 321 || pdgIdAbs == 130 || pdgIdAbs == 310;
  }

  void classifyGenProvenance(const reco::GenParticle& gp,
                             double promptLxyCm,
                             int& parentPdgId,
                             signed char& isDecayInFlight,
                             signed char& provenanceStatus) {
    const reco::Candidate* mother = (gp.numberOfMothers() > 0) ? gp.mother(0) : nullptr;
    parentPdgId = mother ? mother->pdgId() : 0;

    const reco::Candidate* ancestor = mother;
    int guard = 0;
    while (ancestor && std::abs(ancestor->pdgId()) == 13 && guard < 20) {
      ancestor = (ancestor->numberOfMothers() > 0) ? ancestor->mother(0) : nullptr;
      ++guard;
    }

    isDecayInFlight = 0;
    if (ancestor)
      isDecayInFlight = isDifParentPdgIdLocal(std::abs(ancestor->pdgId())) ? 1 : 0;

    const double lXY = std::hypot(gp.vx(), gp.vy());
    if (lXY < promptLxyCm)
      provenanceStatus = 0;  // prompt
    else if (isDecayInFlight == 1)
      provenanceStatus = 1;  // decay in flight
    else
      provenanceStatus = 2;  // other displaced
  }
}  // anonymous namespace

// ---------------------------------------------------------------------------
void DataROOTDumper2AllInput::assignSimTruth(
    const edm::Event& /*iEvent*/,
    const edm::Handle<MuonDigiCollection<DTLayerId, DTDigiSimLink>>& dtLinksH,
    const edm::Handle<edm::DetSetVector<RPCDigiSimLink>>& rpcLinksH,
    const edm::Handle<edm::DetSetVector<StripDigiSimLink>>& cscLinksH,
  const edm::Handle<edm::PSimHitContainer>& dtSimHitsH,
  const edm::Handle<edm::PSimHitContainer>& rpcSimHitsH,
  const edm::Handle<edm::PSimHitContainer>& cscSimHitsH,
    const std::map<uint64_t, int>& simTrackKeyToGenIdx,
    const std::map<uint64_t, int>& simTrackKeyToTpIdx,
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
  std::set<uint64_t>& simTrackKeysWithStub) {

  const unsigned int nStubs = stub_detId.size();
  stub_trackId.assign(nStubs, 0);
  stub_tpId.assign(nStubs, 0);
  stub_ambiguous.assign(nStubs, 0);
  stub_tof.assign(nStubs, -999.f);
  stub_tofSpread.assign(nStubs, 0.f);
  stub_nSimHit.assign(nStubs, 0);
  stub_bestSimTrackId.assign(nStubs, 0);
  stub_bestSimTrackEventId.assign(nStubs, -1);
  stub_truthNContributors.assign(nStubs, 0);
  stub_truthBestVotes.assign(nStubs, 0);
  stub_truthSecondVotes.assign(nStubs, 0);
  stub_truthBestFraction.assign(nStubs, -1.f);
  stub_truthMargin.assign(nStubs, -1);
  // 0 = n/a (a selected-muon link was assigned), else see enum below.
  stub_noSelectedMuonReason.assign(nStubs, 0);
  stub_dualIdRelation.assign(nStubs, 0);

  for (unsigned int iStub = 0; iStub < nStubs; ++iStub) {
    uint32_t detIdRaw = stub_detId[iStub];
    DetId detId(detIdRaw);
    if (detId.det() != DetId::Muon)
      continue;

    double globalPhi = omtfConfig->procHwPhiToGlobalPhi(stub_phiHw[iStub],
                                                         static_cast<int>(processorPhiZero));

    std::map<uint64_t, unsigned> votes;

    switch (detId.subdetId()) {
      case MuonSubdetId::RPC:
        accumRPC(detIdRaw, globalPhi, *rpcLinksH, *georpc_, votes);
        if (rpcSimHitsH.isValid()) {
          std::vector<float> tofs;
          collectRPCTof(detIdRaw, globalPhi, *rpcSimHitsH, *georpc_, tofs);
          summarizeTof(tofs, stub_tof[iStub], stub_tofSpread[iStub], stub_nSimHit[iStub]);
        }
        break;
      case MuonSubdetId::DT:
        accumDT(detIdRaw, globalPhi, *dtLinksH, *geodt_, votes);
        if (dtSimHitsH.isValid()) {
          std::vector<float> tofs;
          collectDTTof(detIdRaw, globalPhi, *dtSimHitsH, *geodt_, tofs);
          summarizeTof(tofs, stub_tof[iStub], stub_tofSpread[iStub], stub_nSimHit[iStub]);
        }
        break;
      case MuonSubdetId::CSC:
        accumCSC(detIdRaw, globalPhi, *cscLinksH, *geocsc_, votes);
        if (cscSimHitsH.isValid()) {
          std::vector<float> tofs;
          collectCSCTof(detIdRaw, globalPhi, *cscSimHitsH, *geocsc_, tofs);
          summarizeTof(tofs, stub_tof[iStub], stub_tofSpread[iStub], stub_nSimHit[iStub]);
        }
        break;
      default:
        break;
    }

    if (votes.empty()) {
      stub_noSelectedMuonReason[iStub] = 1;  // no matching SimTrack contributor at all
      continue;
    }

    // Find dominant and runner-up SimTrack (key = packed trackId,eventId)
    uint64_t bestKey = 0, secondKey = 0;
    unsigned bestCnt = 0, secondCnt = 0;
    unsigned total = 0;
    for (const auto& kv : votes) {
      total += kv.second;
      if (kv.second > bestCnt) {
        secondCnt = bestCnt;
        secondKey = bestKey;
        bestCnt = kv.second;
        bestKey = kv.first;
      } else if (kv.second > secondCnt) {
        secondCnt = kv.second;
        secondKey = kv.first;
      }
    }
    (void)secondKey;

    const uint32_t bestId = static_cast<uint32_t>(bestKey >> 32);
    const int bestEventId = static_cast<int>(static_cast<uint32_t>(bestKey & 0xFFFFFFFFULL));

    simTrackKeysWithStub.insert(bestKey);

    // Map (trackId,eventId) to gen-muon / TpMuon 1-indexed (0 = no correspondence)
    int genIdx = 0;
    auto itGen = simTrackKeyToGenIdx.find(bestKey);
    if (itGen != simTrackKeyToGenIdx.end())
      genIdx = itGen->second;

    int tpIdx = 0;
    auto itTp = simTrackKeyToTpIdx.find(bestKey);
    if (itTp != simTrackKeyToTpIdx.end())
      tpIdx = itTp->second;

    stub_trackId[iStub]   = static_cast<signed char>(genIdx);
    stub_tpId[iStub]      = static_cast<short>(tpIdx);
    // ambiguous if best track covers less than half the matched digis
    const bool ambiguous = (total > 0 && bestCnt * 2 < total);
    stub_ambiguous[iStub] = ambiguous ? 1 : 0;

    stub_bestSimTrackId[iStub] = bestId;
    stub_bestSimTrackEventId[iStub] = bestEventId;
    stub_truthNContributors[iStub] = static_cast<short>(std::min<size_t>(votes.size(), 32767));
    stub_truthBestVotes[iStub] = static_cast<short>(std::min<unsigned>(bestCnt, 32767));
    stub_truthSecondVotes[iStub] = static_cast<short>(std::min<unsigned>(secondCnt, 32767));
    stub_truthBestFraction[iStub] = (total > 0) ? static_cast<float>(bestCnt) / static_cast<float>(total) : -1.f;
    stub_truthMargin[iStub] = static_cast<short>(static_cast<int>(bestCnt) - static_cast<int>(secondCnt));

    if (genIdx == 0 && tpIdx == 0) {
      // Classify why this dominant contributor did not resolve to a selected muon.
      if (ambiguous) {
        stub_noSelectedMuonReason[iStub] = 5;  // no stable dominant contributor
      } else {
        // Look up the dominant contributor in the full (unselected) muon-TP map.
        auto itAll = allMuonTpByKey_.find(bestKey);
        if (itAll == allMuonTpByKey_.end()) {
          stub_noSelectedMuonReason[iStub] = 6;  // dominant contributor is not a muon-type TrackingParticle
        } else {
          const TrackingParticle* tp = itAll->second;
          if (tp->pt() <= 1.5f) {
            stub_noSelectedMuonReason[iStub] = 2;  // matching muon below pT selection
          } else if (std::abs(tp->eta()) >= 1.6f) {
            stub_noSelectedMuonReason[iStub] = 3;  // matching muon outside eta selection
          } else {
            stub_noSelectedMuonReason[iStub] = 7;  // unknown/unresolved (should be rare)
          }
        }
      }
    } else if (genIdx > 0 && tpIdx > 0) {
      // Both a GenMuon-level and a TpMuon-level truth link exist for this stub: classify
      // whether they refer to the SAME physical hard-scatter muon (item 1).
      if (tpIdx - 1 < static_cast<int>(TpMuon_genMatchStatus.size())) {
        const signed char matchStatus = TpMuon_genMatchStatus[tpIdx - 1];
        const int tpGenMuonIdx = TpMuon_genMuonIdx[tpIdx - 1];  // 0-indexed, -1 = none
        if (matchStatus == 2 || matchStatus == 3) {
          stub_dualIdRelation[iStub] = 3;  // ambiguous/unresolved
        } else if (tpGenMuonIdx == genIdx - 1) {
          stub_dualIdRelation[iStub] = 1;  // same physical muon
        } else {
          stub_dualIdRelation[iStub] = 2;  // different physical muons
        }
      } else {
        stub_dualIdRelation[iStub] = 3;
      }
    }
  }
}

// ---------------------------------------------------------------------------
void DataROOTDumper2AllInput::observeEventEnd(
    const edm::Event& iEvent,
    std::unique_ptr<l1t::RegionalMuonCandBxCollection>& finalCandidates) {

  const unsigned int runNum = iEvent.id().run();
  const unsigned int lumiNum = iEvent.id().luminosityBlock();
  const unsigned int eventNum = iEvent.id().event();
  const unsigned long long eventNum64 = static_cast<unsigned long long>(iEvent.id().event());

  reg_pu_nTpMuonInTime = 0;
  reg_pu_nTpMuonOOT = 0;
  reg_pu_src_bx.clear();
  reg_pu_src_run.clear();
  reg_pu_src_lumi.clear();
  reg_pu_src_event.clear();
  reg_pu_src_hash.clear();
  reg_pu_src_eventHash.clear();
  reg_pu_src_occurrenceHash.clear();

  // First-occurrence start offset of each source BX group within reg_pu_src_* (assumes the
  // Mixing-module convention that EncodedEventId::event() is the 1-indexed position of a
  // sub-event within its BX's PileupSummaryInfo::getPU_EventID() list; event()==0 is reserved
  // for the hard-scatter). Used below to resolve TpMuon_puSourceIdx. See item 4 limitations:
  // this does NOT yet carry a source-FILE identity (fileGuid/LFN), so two different MinBias
  // input files that happen to reuse the same (run,lumi,event) numbering are not distinguished.
  std::map<int, int> startOffsetForBx;

  if (!pileupInfoTag_.label().empty()) {
    edm::Handle<std::vector<PileupSummaryInfo>> pileupInfoH;
    iEvent.getByLabel(pileupInfoTag_, pileupInfoH);
    if (pileupInfoH.isValid()) {
      for (const auto& pu : *pileupInfoH) {
        int bx = pu.getBunchCrossing();
        if (bx > 127)
          bx = 127;
        else if (bx < -127)
          bx = -127;

        if (startOffsetForBx.find(bx) == startOffsetForBx.end())
          startOffsetForBx[bx] = static_cast<int>(reg_pu_src_bx.size());

        for (const auto& srcId : pu.getPU_EventID()) {
          const uint32_t srcRun = srcId.run();
          const uint32_t srcLumi = srcId.luminosityBlock();
          const uint64_t srcEvent = srcId.event();
          reg_pu_src_bx.push_back(static_cast<signed char>(bx));
          reg_pu_src_run.push_back(srcRun);
          reg_pu_src_lumi.push_back(srcLumi);
          reg_pu_src_event.push_back(static_cast<unsigned long long>(srcEvent));
          const unsigned long long occHash =
              static_cast<unsigned long long>(makeSourceOccurrenceHash(bx, srcRun, srcLumi, srcEvent));
          reg_pu_src_hash.push_back(occHash);            // legacy alias, same formula
          reg_pu_src_occurrenceHash.push_back(occHash);
          reg_pu_src_eventHash.push_back(
              static_cast<unsigned long long>(makeSourceEventHash(srcRun, srcLumi, srcEvent)));
        }
      }
    }
  }

  // --- Optionally build SimTrack → gen-muon index map ---
  // simTrackKeyToGenIdx[packTrackKey(trackId,eventId)] = 1-indexed gen-muon index (0=none)
  std::map<uint64_t, int> simTrackKeyToGenIdx;
  // simTrackKeyToTpIdx[packTrackKey(trackId,eventId)] = 1-indexed selected TpMuon index (0=none)
  std::map<uint64_t, int> simTrackKeyToTpIdx;

  std::vector<const TrackingParticle*> selectedTpMuons;

  // Event-level TrackingParticle-muon truth coverage counters (item 9). Reset every event;
  // computed over the FULL (unselected) TrackingParticle collection below.
  nTpMuonAll = 0;
  nTpMuonPtGt0p5 = 0;
  nTpMuonPtGt1p5 = 0;
  nTpMuonPtGt5 = 0;
  nTpMuonInOmtfAcceptance = 0;
  nTpMuonBelowSelectionWithOmtfStub = 0;
  allMuonTpByKey_.clear();
  std::vector<const TrackingParticle*> belowSelectionMuonTps;  // for nTpMuonBelowSelectionWithOmtfStub

  edm::Handle<MuonDigiCollection<DTLayerId, DTDigiSimLink>> dtLinksH;
  edm::Handle<edm::DetSetVector<RPCDigiSimLink>>            rpcLinksH;
  edm::Handle<edm::DetSetVector<StripDigiSimLink>>          cscLinksH;
  edm::Handle<edm::PSimHitContainer>                        dtSimHitsH;
  edm::Handle<edm::PSimHitContainer>                        rpcSimHitsH;
  edm::Handle<edm::PSimHitContainer>                        cscSimHitsH;
  edm::Handle<reco::GenParticleCollection>                  genParticlesH;
  bool simHandlesOk = false;

  if (doSimTruth_) {
    iEvent.getByLabel(dtSimLinkTag_,  dtLinksH);
    iEvent.getByLabel(rpcSimLinkTag_, rpcLinksH);
    iEvent.getByLabel(cscSimLinkTag_, cscLinksH);
    iEvent.getByLabel(dtSimHitTag_,   dtSimHitsH);
    iEvent.getByLabel(rpcSimHitTag_,  rpcSimHitsH);
    iEvent.getByLabel(cscSimHitTag_,  cscSimHitsH);
    simHandlesOk = dtLinksH.isValid() && rpcLinksH.isValid() && cscLinksH.isValid();

    if (simHandlesOk) {
      // Build SimTrack (trackId,eventId) → gen-muon 1-index map.
      // We consider gen muons with |pdgId|==13 as "gen muons".
      edm::Handle<edm::SimTrackContainer>   simTracksH;
      iEvent.getByLabel(simTrackTag_,    simTracksH);
      iEvent.getByLabel(genParticleTag_, genParticlesH);

      if (simTracksH.isValid() && genParticlesH.isValid()) {
        // Build gen-muon list (1-indexed)
        int genMuonIdx = 0;
        for (const auto& gp : *genParticlesH) {
          if (std::abs(gp.pdgId()) != 13)
            continue;
          genMuonIdx++;  // 1-indexed
          // Find all SimTracks that are this gen muon (match via momentum proximity, ΔR<0.05).
          // NOTE: this is a KINEMATIC proxy retained for reg_stub_trackId backward compatibility;
          // TpMuon_genMuonIdx below uses the EXACT TrackingParticle::genParticles() Ref instead
          // (see item 1: "prefer exact simulation identity over kinematic matching"). The two
          // should agree in the vast majority of cases; disagreement is a signal worth auditing.
          for (const auto& st : *simTracksH) {
            if (std::abs(st.type()) != 13)
              continue;
            float dEta = st.momentum().eta() - gp.eta();
            float dPhi = st.momentum().phi() - gp.phi();
            while (dPhi >  M_PI) dPhi -= 2 * M_PI;
            while (dPhi < -M_PI) dPhi += 2 * M_PI;
            float dR2 = dEta * dEta + dPhi * dPhi;
            if (dR2 < 0.0025f) {  // dR < 0.05
              const uint64_t key = packTrackKey(st.trackId(), st.eventId().rawId());
              auto it = simTrackKeyToGenIdx.find(key);
              if (it == simTrackKeyToGenIdx.end())
                simTrackKeyToGenIdx[key] = genMuonIdx;
            }
          }
        }
      }

      // Build the full (unselected) muon-TrackingParticle lookup + coverage counters, then the
      // selected TpMuon table and its SimTrack (trackId,eventId) → TpMuon index map.
      if (!trackingParticleTag_.label().empty()) {
        edm::Handle<TrackingParticleCollection> trackingParticlesH;
        iEvent.getByLabel(trackingParticleTag_, trackingParticlesH);

        if (trackingParticlesH.isValid()) {
          for (const auto& tp : *trackingParticlesH) {
            if (std::abs(tp.pdgId()) != 13)
              continue;

            nTpMuonAll++;
            const double tpPt = tp.pt();
            const double tpEta = std::abs(tp.momentum().eta());
            if (tpPt > 0.5) nTpMuonPtGt0p5++;
            if (tpPt > 1.5) nTpMuonPtGt1p5++;
            if (tpPt > 5.0) nTpMuonPtGt5++;
            if (tpEta >= 0.7 && tpEta <= 1.31) nTpMuonInOmtfAcceptance++;

            for (const auto& g4Track : tp.g4Tracks()) {
              const uint64_t key = packTrackKey(g4Track.trackId(), g4Track.eventId().rawId());
              allMuonTpByKey_.emplace(key, &tp);
            }

            const bool passesSelection = (tpPt > 1.5) && (tpEta < 1.6);
            if (!passesSelection) {
              belowSelectionMuonTps.push_back(&tp);
              continue;
            }
            selectedTpMuons.push_back(&tp);
          }

          TpMuon_pt.clear();
          TpMuon_eta.clear();
          TpMuon_phi.clear();
          TpMuon_charge.clear();
          TpMuon_dxy.clear();
          TpMuon_lxy.clear();
          TpMuon_vx.clear();
          TpMuon_vy.clear();
          TpMuon_vz.clear();
          TpMuon_t.clear();
          TpMuon_dxyHelix.clear();
          TpMuon_eventId.clear();
          TpMuon_bunchCrossing.clear();
          TpMuon_isPileup.clear();
          TpMuon_isInTime.clear();
          TpMuon_originClass.clear();
          TpMuon_simTrackId.clear();
          TpMuon_simTrackEventId.clear();
          TpMuon_genMuonIdx.clear();
          TpMuon_genMatchStatus.clear();
          TpMuon_parentPdgId.clear();
          TpMuon_isDecayInFlight.clear();
          TpMuon_provenanceStatus.clear();
          TpMuon_puSourceIdx.clear();
          TpMuon_propEtaSt1.clear();
          TpMuon_propPhiSt1.clear();
          TpMuon_propEtaSt2.clear();
          TpMuon_propPhiSt2.clear();
          TpMuon_propOk.clear();
          TpMuon_propInOmtfSt1.clear();
          TpMuon_propInOmtfSt2.clear();
          TpMuon_propFailureCode.clear();
          TpMuon_propSurfaceCodeSt1.clear();
          TpMuon_propSurfaceCodeSt2.clear();

          // Row index (0-indexed) each selected TpMuon exactly corresponds to in the GenMuon
          // NanoAOD table, computed via TrackingParticle::genParticles() (exact simulation
          // identity, NOT kinematic matching -- item 1). genMuonNanoTable's row selection cut is
          // "abs(pdgId)==13 && status==1"; the prefix-count below MUST use the identical cut over
          // the identical genParticlesTag_ collection for the indices to line up.
          std::vector<int> tentativeGenIdx;  // -1 = none, else 0-indexed GenMuon row
          tentativeGenIdx.reserve(selectedTpMuons.size());
          if (genParticlesH.isValid()) {
            for (const auto* tp : selectedTpMuons) {
              int rowIdx = -1;
              bool conflicting = false;
              for (const auto& ref : tp->genParticles()) {
                if (ref.isNull() || !ref.isAvailable())
                  continue;
                const reco::GenParticle& gp = *ref;
                if (std::abs(gp.pdgId()) != 13 || gp.status() != 1)
                  continue;
                int row = 0;
                for (size_t k = 0; k < ref.key(); ++k) {
                  const auto& other = (*genParticlesH)[k];
                  if (std::abs(other.pdgId()) == 13 && other.status() == 1)
                    row++;
                }
                if (rowIdx == -1) {
                  rowIdx = row;
                } else if (rowIdx != row) {
                  conflicting = true;
                }
              }
              tentativeGenIdx.push_back(conflicting ? -2 : rowIdx);  // -2 sentinel = conflicting
            }
          } else {
            tentativeGenIdx.assign(selectedTpMuons.size(), -1);
          }

          // Ambiguity pass: count how many TpMuon rows claim the same nonzero GenMuon row.
          std::map<int, int> genRowClaimCount;
          for (int idx : tentativeGenIdx)
            if (idx >= 0)
              genRowClaimCount[idx]++;

          int tpIdx = 0;
          for (size_t iTp = 0; iTp < selectedTpMuons.size(); ++iTp) {
            const TrackingParticle* tp = selectedTpMuons[iTp];
            tpIdx++;

            TpMuon_pt.push_back(tp->pt());
            TpMuon_eta.push_back(tp->momentum().eta());
            TpMuon_phi.push_back(tp->momentum().phi());
            TpMuon_charge.push_back(static_cast<signed char>(tp->charge()));

            float dxy = -999.f;
            float lxy = -999.f;
            float vx = -999.f;
            float vy = -999.f;
            float vz = -999.f;
            float vt = -999.f;
            if (tp->parentVertex().isNonnull()) {
              dxy = tp->dxy();
              lxy = tp->parentVertex()->position().Rho();
              vx = tp->parentVertex()->position().x();
              vy = tp->parentVertex()->position().y();
              vz = tp->parentVertex()->position().z();
              vt = tp->parentVertex()->position().t();
            }
            TpMuon_dxy.push_back(dxy);
            TpMuon_lxy.push_back(lxy);
            TpMuon_vx.push_back(vx);
            TpMuon_vy.push_back(vy);
            TpMuon_vz.push_back(vz);
            TpMuon_t.push_back(vt);

            // Curvature-consistent (helix-based) impact parameter, same formula as
            // CandidateSimMuonMatcher::MatchingResult's SimTrack-path constructor:
            //   rg = -pT/(0.003*B*q), B = 3.811 T
            //   center = (vx - rg*sin(phi), vy + rg*cos(phi))
            //   dxyHelix = rg + q*|center|
            float dxyHelix = -999.f;
            if (tp->parentVertex().isNonnull() && tp->charge() != 0) {
              const double B = 3.811;  // Tesla
              const double genPt = tp->pt();
              const double genPhi = tp->momentum().phi();
              const double genCharge = tp->charge();
              const double rg = -genPt / (0.003 * B * genCharge);
              const double cx = static_cast<double>(vx) - rg * std::sin(genPhi);
              const double cy = static_cast<double>(vy) + rg * std::cos(genPhi);
              dxyHelix = static_cast<float>(rg + genCharge * std::sqrt(cx * cx + cy * cy));
            }
            TpMuon_dxyHelix.push_back(dxyHelix);

            const int tpEventId = tp->eventId().event();
            const int tpBx = tp->eventId().bunchCrossing();
            const bool isPileup = (tpEventId != 0);
            const bool isInTime = (tpBx == 0);
            signed char originClass = 3;
            if (!isPileup && isInTime)
              originClass = 0;
            else if (isPileup && isInTime)
              originClass = 1;
            else if (isPileup && !isInTime)
              originClass = 2;

            TpMuon_eventId.push_back(tpEventId);
            TpMuon_bunchCrossing.push_back(static_cast<signed char>(tpBx));
            TpMuon_isPileup.push_back(isPileup ? 1 : 0);
            TpMuon_isInTime.push_back(isInTime ? 1 : 0);
            TpMuon_originClass.push_back(originClass);

            // Exact simulation identity (item 1): first backing g4Track SimTrack.
            unsigned int simTrackId = 0;
            int simTrackEventId = -1;
            const auto& g4Tracks = tp->g4Tracks();
            if (!g4Tracks.empty()) {
              simTrackId = g4Tracks.front().trackId();
              simTrackEventId = g4Tracks.front().eventId().rawId();
            }
            TpMuon_simTrackId.push_back(simTrackId);
            TpMuon_simTrackEventId.push_back(simTrackEventId);

            // Exact GenMuon<->TpMuon relation (item 1).
            const int tentative = tentativeGenIdx[iTp];
            int genMuonIdxOut = -1;
            signed char genMatchStatus = 0;
            if (tentative == -2) {
              genMatchStatus = 3;  // conflicting: this TP's own genParticles() refs disagree
            } else if (tentative >= 0) {
              genMuonIdxOut = tentative;
              genMatchStatus = (genRowClaimCount[tentative] > 1) ? 2 : 1;  // ambiguous vs unique
            }
            TpMuon_genMuonIdx.push_back(genMuonIdxOut);
            TpMuon_genMatchStatus.push_back(genMatchStatus);

            // Pileup ancestry (item 5): only derivable when an exact GenParticle Ref is retained.
            int parentPdgId = 0;
            signed char isDecayInFlight = -1;  // -1 = unknown/unresolved
            signed char provenanceStatus = 3;  // 3 = unknown/unresolved
            if (!tp->genParticles().empty() && tp->genParticles()[0].isAvailable()) {
              classifyGenProvenance(*tp->genParticles()[0], 0.01, parentPdgId, isDecayInFlight, provenanceStatus);
            }
            TpMuon_parentPdgId.push_back(parentPdgId);
            TpMuon_isDecayInFlight.push_back(isDecayInFlight);
            TpMuon_provenanceStatus.push_back(provenanceStatus);

            // Pileup source-interaction relation (item 4): assumes the Mixing convention
            // documented above (startOffsetForBx). -1 when unresolved/hard-scatter.
            int puSourceIdx = -1;
            if (isPileup) {
              auto itOff = startOffsetForBx.find(tpBx);
              if (itOff != startOffsetForBx.end()) {
                const int idx = itOff->second + (tpEventId - 1);
                if (idx >= 0 && idx < static_cast<int>(reg_pu_src_bx.size()) &&
                    reg_pu_src_bx[idx] == static_cast<signed char>(tpBx)) {
                  puSourceIdx = idx;
                }
              }
            }
            TpMuon_puSourceIdx.push_back(puSourceIdx);

            float propEtaSt1 = -999.f;
            float propPhiSt1 = -999.f;
            float propEtaSt2 = -999.f;
            float propPhiSt2 = -999.f;
            uint8_t propOk = 0;
            uint8_t inOmtfSt1 = 0;
            uint8_t inOmtfSt2 = 0;
            signed char propFailureCode = 2;    // default: propagation failed
            signed char propSurfaceCodeSt1 = 0; // 0 = unknown/proxy (see item 7 limitations)
            signed char propSurfaceCodeSt2 = 0;
            auto* m = matcher();
            // CandidateSimMuonMatcher only loads its Propagator ESHandle in beginRun() when
            // configured with candidateSimMuonMatcherType "withPropagator"/"simplePropagation"
            // (see CandidateSimMuonMatcher::beginRun). In "simpleMatching" mode (the default for
            // this GEN,SIM,DIGI,L1 dumper workflow -- see customize_omtf_dumper.py -- no
            // SteppingHelixPropagator ESProducer is loaded), the propagator handle is left
            // default-constructed/invalid, and calling propagate()/atStation2() on it segfaults.
            // Only attempt propagation when the matcher was actually configured with a propagator.
            const bool matcherHasPropagator =
                m != nullptr && (m->getMatchingType() == CandidateSimMuonMatcher::MatchingType::withPropagator ||
                                 m->getMatchingType() == CandidateSimMuonMatcher::MatchingType::simplePropagation);
            if (matcherHasPropagator) {
              // Use the same propagation entry point as CandidateSimMuonMatcher
              // to avoid geometry corner-case crashes on direct station calls.
              const auto tsosSt2 = m->propagate(*tp);
              if (tsosSt2.isValid()) {
                propEtaSt2 = tsosSt2.globalPosition().eta();
                propPhiSt2 = tsosSt2.globalPosition().phi();
                inOmtfSt2 = (std::abs(propEtaSt2) >= 0.7f && std::abs(propEtaSt2) <= 1.31f) ? 1 : 0;
                propOk = 1;
                propFailureCode = 0;

                // Station-1 labels are optional in this path; use station-2 as a conservative
                // proxy when explicit MB1 propagation is unavailable (propagationVersion=1: both
                // surface codes are "unknown/proxy" today, NOT a validated MB1-specific surface).
                propEtaSt1 = propEtaSt2;
                propPhiSt1 = propPhiSt2;
                inOmtfSt1 = inOmtfSt2;
              } else {
                propFailureCode = 3;  // did not intersect surface / invalid TSOS
              }
            } else if (m == nullptr) {
              propFailureCode = 1;  // matcher not available (invalid initial state context)
            } else {
              propFailureCode = 6;  // matcher configured without a propagator (e.g. simpleMatching)
            }

            TpMuon_propEtaSt1.push_back(propEtaSt1);
            TpMuon_propPhiSt1.push_back(propPhiSt1);
            TpMuon_propEtaSt2.push_back(propEtaSt2);
            TpMuon_propPhiSt2.push_back(propPhiSt2);
            TpMuon_propOk.push_back(propOk);
            TpMuon_propInOmtfSt1.push_back(inOmtfSt1);
            TpMuon_propInOmtfSt2.push_back(inOmtfSt2);
            TpMuon_propFailureCode.push_back(propFailureCode);
            TpMuon_propSurfaceCodeSt1.push_back(propSurfaceCodeSt1);
            TpMuon_propSurfaceCodeSt2.push_back(propSurfaceCodeSt2);

            if (originClass == 1)
              reg_pu_nTpMuonInTime++;
            else if (originClass == 2)
              reg_pu_nTpMuonOOT++;

            for (const auto& g4Track : tp->g4Tracks()) {
              const uint64_t key = packTrackKey(g4Track.trackId(), g4Track.eventId().rawId());
              auto it = simTrackKeyToTpIdx.find(key);
              if (it == simTrackKeyToTpIdx.end())
                simTrackKeyToTpIdx[key] = tpIdx;
            }
          }
        }
      }
    }
  }

  if (!simHandlesOk || selectedTpMuons.empty()) {
    TpMuon_pt.clear();
    TpMuon_eta.clear();
    TpMuon_phi.clear();
    TpMuon_charge.clear();
    TpMuon_dxy.clear();
    TpMuon_lxy.clear();
    TpMuon_vx.clear();
    TpMuon_vy.clear();
    TpMuon_vz.clear();
    TpMuon_t.clear();
    TpMuon_dxyHelix.clear();
    TpMuon_eventId.clear();
    TpMuon_bunchCrossing.clear();
    TpMuon_isPileup.clear();
    TpMuon_isInTime.clear();
    TpMuon_originClass.clear();
    TpMuon_simTrackId.clear();
    TpMuon_simTrackEventId.clear();
    TpMuon_genMuonIdx.clear();
    TpMuon_genMatchStatus.clear();
    TpMuon_parentPdgId.clear();
    TpMuon_isDecayInFlight.clear();
    TpMuon_provenanceStatus.clear();
    TpMuon_puSourceIdx.clear();
    TpMuon_propEtaSt1.clear();
    TpMuon_propPhiSt1.clear();
    TpMuon_propEtaSt2.clear();
    TpMuon_propPhiSt2.clear();
    TpMuon_propOk.clear();
    TpMuon_propInOmtfSt1.clear();
    TpMuon_propInOmtfSt2.clear();
    TpMuon_propFailureCode.clear();
    TpMuon_propSurfaceCodeSt1.clear();
    TpMuon_propSurfaceCodeSt2.clear();
  }

  // --- Phase 1: run per-region sim-truth assignment WITHOUT filling, so that the aggregated
  // simTrackKeysWithStub set (across all processor regions in this event) is available before
  // computing nTpMuonBelowSelectionWithOmtfStub (item 9), which must be known before any Fill().
  struct SimTruthResult {
    std::vector<signed char> trackId;
    std::vector<short> tpId;
    std::vector<uint8_t> ambiguous;
    std::vector<float> tof;
    std::vector<float> tofSpread;
    std::vector<signed char> nSimHit;
    std::vector<unsigned int> bestSimTrackId;
    std::vector<int> bestSimTrackEventId;
    std::vector<short> truthNContributors;
    std::vector<short> truthBestVotes;
    std::vector<short> truthSecondVotes;
    std::vector<float> truthBestFraction;
    std::vector<short> truthMargin;
    std::vector<signed char> noSelectedMuonReason;
    std::vector<signed char> dualIdRelation;
  };
  std::vector<SimTruthResult> perRegionTruth(cachedRegions.size());
  std::set<uint64_t> simTrackKeysWithStub;

  if (doSimTruth_ && simHandlesOk) {
    for (size_t iRegion = 0; iRegion < cachedRegions.size(); ++iRegion) {
      auto& region = cachedRegions[iRegion];
      auto& res = perRegionTruth[iRegion];
      unsigned int procPhiZero =
          static_cast<unsigned int>(OMTFinputMaker::getProcessorPhiZero(omtfConfig, region.iProcessor));
      assignSimTruth(iEvent, dtLinksH, rpcLinksH, cscLinksH,
                     dtSimHitsH, rpcSimHitsH, cscSimHitsH,
                     simTrackKeyToGenIdx, simTrackKeyToTpIdx, procPhiZero,
                     region.stub_detId, region.stub_phiHw,
                     res.trackId, res.tpId, res.ambiguous,
                     res.tof, res.tofSpread, res.nSimHit,
                     res.bestSimTrackId, res.bestSimTrackEventId,
                     res.truthNContributors, res.truthBestVotes, res.truthSecondVotes,
                     res.truthBestFraction, res.truthMargin,
                     res.noSelectedMuonReason, res.dualIdRelation,
                     simTrackKeysWithStub);
    }

    // nTpMuonBelowSelectionWithOmtfStub (item 9): below-selection muon TPs that have >=1 stub
    // in this event, cross-checked via the aggregated dominant-contributor key set above.
    for (const auto* tp : belowSelectionMuonTps) {
      bool hasStub = false;
      for (const auto& g4Track : tp->g4Tracks()) {
        const uint64_t key = packTrackKey(g4Track.trackId(), g4Track.eventId().rawId());
        if (simTrackKeysWithStub.count(key)) {
          hasStub = true;
          break;
        }
      }
      if (hasStub)
        nTpMuonBelowSelectionWithOmtfStub++;
    }
  }

  // --- Phase 2: flush the per-event cache into OMTFAllInputTree ---
  for (size_t iRegion = 0; iRegion < cachedRegions.size(); ++iRegion) {
    auto& region = cachedRegions[iRegion];
    reg_runNum     = runNum;
    reg_lumiNum    = lumiNum;
    reg_eventNum64 = eventNum64;
    reg_eventNum   = eventNum;
    reg_iProcessor = region.iProcessor;
    reg_mtfType    = region.mtfType;
    reg_endcap     = region.endcap;

    reg_stub_layer   = std::move(region.stub_layer);
    reg_stub_phiHw   = std::move(region.stub_phiHw);
    reg_stub_phiBHw  = std::move(region.stub_phiBHw);
    reg_stub_etaHw   = std::move(region.stub_etaHw);
    reg_stub_r       = std::move(region.stub_r);
    reg_stub_quality = std::move(region.stub_quality);
    reg_stub_type    = std::move(region.stub_type);
    reg_stub_bx      = std::move(region.stub_bx);
    reg_stub_detId   = std::move(region.stub_detId);
    reg_stub_primitiveId = std::move(region.stub_primitiveId);
    reg_stub_sourceIndex = std::move(region.stub_sourceIndex);
    // sourceCollection/sourceDetId/sourceBx are, by definition, the same values as
    // reg_stub_type/reg_stub_detId/reg_stub_bx -- persisted as their own branches (rather than
    // relying on consumers to know the aliasing) so the primitiveId v2 recipe is self-contained.
    reg_stub_sourceCollection = reg_stub_type;
    reg_stub_sourceDetId      = reg_stub_detId;
    reg_stub_sourceBx         = reg_stub_bx;

    const unsigned int nStubs = reg_stub_layer.size();

    if (doSimTruth_ && simHandlesOk) {
      auto& res = perRegionTruth[iRegion];
      reg_stub_trackId   = std::move(res.trackId);
      reg_stub_tpId      = std::move(res.tpId);
      reg_stub_ambiguous = std::move(res.ambiguous);
      reg_stub_tof       = std::move(res.tof);
      reg_stub_tofSpread = std::move(res.tofSpread);
      reg_stub_nSimHit   = std::move(res.nSimHit);
      reg_stub_bestSimTrackId      = std::move(res.bestSimTrackId);
      reg_stub_bestSimTrackEventId = std::move(res.bestSimTrackEventId);
      reg_stub_truthNContributors  = std::move(res.truthNContributors);
      reg_stub_truthBestVotes      = std::move(res.truthBestVotes);
      reg_stub_truthSecondVotes    = std::move(res.truthSecondVotes);
      reg_stub_truthBestFraction   = std::move(res.truthBestFraction);
      reg_stub_truthMargin         = std::move(res.truthMargin);
      reg_stub_noSelectedMuonReason = std::move(res.noSelectedMuonReason);
      reg_stub_dualIdRelation        = std::move(res.dualIdRelation);
    } else {
      reg_stub_trackId.assign(nStubs, 0);
      reg_stub_tpId.assign(nStubs, 0);
      reg_stub_ambiguous.assign(nStubs, 0);
      reg_stub_tof.assign(nStubs, -999.f);
      reg_stub_tofSpread.assign(nStubs, 0.f);
      reg_stub_nSimHit.assign(nStubs, 0);
      reg_stub_bestSimTrackId.assign(nStubs, 0);
      reg_stub_bestSimTrackEventId.assign(nStubs, -1);
      reg_stub_truthNContributors.assign(nStubs, 0);
      reg_stub_truthBestVotes.assign(nStubs, 0);
      reg_stub_truthSecondVotes.assign(nStubs, 0);
      reg_stub_truthBestFraction.assign(nStubs, -1.f);
      reg_stub_truthMargin.assign(nStubs, -1);
      reg_stub_noSelectedMuonReason.assign(nStubs, 0);
      reg_stub_dualIdRelation.assign(nStubs, 0);
    }

    allInputTree->Fill();
  }
  cachedRegions.clear();

  // Let the base class write OMTFHitsTree (per-candidate logic unchanged)
  DataROOTDumper2::observeEventEnd(iEvent, finalCandidates);
}

// ---------------------------------------------------------------------------
void DataROOTDumper2AllInput::endJob() {
  allInputTree->Write();
  // Base class writes OMTFHitsTree
  DataROOTDumper2::endJob();
}

