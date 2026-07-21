// PrimitiveDigiToFlatTableProducers.cc
//
// NanoAOD FlatTable producers for muon trigger primitive digis:
//
//   DTPhiDigiFlatTableProducer
//     reads L1MuDTChambPhContainer (Run-3 DT phi primitives)
//     variables: bx, wheel, sector, station, phi, phiB, quality, ts2Tag, rpcBit
//
//   Ph2DTPhiDigiFlatTableProducer
//     reads L1Phase2MuDTPhContainer (Phase-2 DT phi primitives)
//     variables: bx, wheel, sector, station, superlayer, phi, phiBend, quality,
//                index, t0, chi2, rpcFlag
//
//   Ph2DTThetaDigiFlatTableProducer
//     reads L1Phase2MuDTThContainer (Phase-2 DT theta primitives)
//     variables: bx, wheel, sector, station, z, k, quality, index, t0, chi2, rpcFlag
//
//   CSCLCTDigiFlatTableProducer
//     reads CSCCorrelatedLCTDigiCollection (MuonDigiCollection<CSCDetId, CSCCorrelatedLCTDigi>)
//     variables: endcap, station, ring, chamber, keywire, strip,
//                pattern, run3Pattern, slope, bend, quality, bx, cscId
//
//   RPCDigiFlatTableProducer
//     reads RPCDigiCollection (MuonDigiCollection<RPCDetId, RPCDigi>)
//     variables: region, ring, station, sector, layer, roll, strip, bx

#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "DataFormats/NanoAOD/interface/FlatTable.h"

// DT Run-3
#include "DataFormats/L1DTTrackFinder/interface/L1MuDTChambPhContainer.h"
#include "DataFormats/L1DTTrackFinder/interface/L1MuDTChambPhDigi.h"

// DT Phase-2
#include "DataFormats/L1DTTrackFinder/interface/L1Phase2MuDTPhContainer.h"
#include "DataFormats/L1DTTrackFinder/interface/L1Phase2MuDTPhDigi.h"
#include "DataFormats/L1DTTrackFinder/interface/L1Phase2MuDTThContainer.h"
#include "DataFormats/L1DTTrackFinder/interface/L1Phase2MuDTThDigi.h"

// CSC
#include "DataFormats/CSCDigi/interface/CSCCorrelatedLCTDigiCollection.h"
#include "DataFormats/CSCDigi/interface/CSCCorrelatedLCTDigi.h"
#include "DataFormats/CSCDigi/interface/CSCConstants.h"
#include "DataFormats/MuonDetId/interface/CSCDetId.h"
#include "Geometry/CSCGeometry/interface/CSCGeometry.h"
#include "Geometry/CSCGeometry/interface/CSCChamber.h"
#include "Geometry/CSCGeometry/interface/CSCLayer.h"
#include "Geometry/CSCGeometry/interface/CSCLayerGeometry.h"

// RPC
#include "DataFormats/RPCDigi/interface/RPCDigiCollection.h"
#include "DataFormats/RPCDigi/interface/RPCDigi.h"
#include "DataFormats/MuonDetId/interface/RPCDetId.h"
#include "Geometry/RPCGeometry/interface/RPCGeometry.h"
#include "Geometry/RPCGeometry/interface/RPCRoll.h"

// Muon geometry ES record (shared by CSC/RPC geometry lookups below)
#include "Geometry/Records/interface/MuonGeometryRecord.h"
#include "FWCore/Utilities/interface/ESGetToken.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

// ============================================================================
// 1. DTPhiDigiFlatTableProducer — Run-3 DT phi primitives
// ============================================================================
class DTPhiDigiFlatTableProducer : public edm::global::EDProducer<> {
public:
  explicit DTPhiDigiFlatTableProducer(const edm::ParameterSet& ps)
      : src_(mayConsume<L1MuDTChambPhContainer>(ps.getParameter<edm::InputTag>("src"))),
        name_(ps.getParameter<std::string>("name")),
        doc_(ps.getParameter<std::string>("doc")) {
    produces<nanoaod::FlatTable>();
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("src", edm::InputTag("simDtTriggerPrimitiveDigis"));
    desc.add<std::string>("name", "DTPhiDigi");
    desc.add<std::string>("doc", "Run-3 DT phi trigger primitives");
    descriptions.addWithDefaultLabel(desc);
  }

  void produce(edm::StreamID, edm::Event& ev, const edm::EventSetup&) const override {
    const auto container = ev.getHandle(src_);
    const auto* digis = (container.isValid()) ? container->getContainer() : nullptr;

    const unsigned int n = digis ? digis->size() : 0;

    std::vector<int16_t> vBx, vWheel, vSector, vStation;
    std::vector<int16_t> vPhi, vPhiB, vQuality, vTs2Tag, vRpcBit;
    std::vector<uint8_t> vHasRpc, vIsSecondTs;
    vBx.reserve(n); vWheel.reserve(n); vSector.reserve(n); vStation.reserve(n);
    vPhi.reserve(n); vPhiB.reserve(n); vQuality.reserve(n);
    vTs2Tag.reserve(n); vRpcBit.reserve(n);
    vHasRpc.reserve(n); vIsSecondTs.reserve(n);

    if (digis) {
      for (const auto& d : *digis) {
        vBx.push_back(d.bxNum());
        vWheel.push_back(d.whNum());
        vSector.push_back(d.scNum());
        vStation.push_back(d.stNum());
        vPhi.push_back(d.phi());
        vPhiB.push_back(d.phiB());
        vQuality.push_back(d.code());
        vTs2Tag.push_back(d.Ts2Tag());
        vRpcBit.push_back(d.RpcBit());
        vHasRpc.push_back(d.RpcBit() != 0);
        vIsSecondTs.push_back(d.Ts2Tag() != 0);
      }
    }

    auto table = std::make_unique<nanoaod::FlatTable>(n, name_, false, false);
    table->setDoc(doc_);
    table->addColumn<int16_t>("bx",      vBx,      "bunch crossing number");
    table->addColumn<int16_t>("wheel",   vWheel,   "DT wheel (-2..+2)");
    table->addColumn<int16_t>("sector",  vSector,  "DT sector (0..11)");
    table->addColumn<int16_t>("station", vStation, "DT station (1..4)");
    table->addColumn<int16_t>("phi",     vPhi,     "radial angle [~1/2048 of pi/6 rad per unit]");
    table->addColumn<int16_t>("phiB",    vPhiB,    "bending angle [10-bit signed, local direction proxy]");
    table->addColumn<int16_t>("quality", vQuality, "PHTF quality code (0..7)");
    table->addColumn<int16_t>("ts2Tag",  vTs2Tag,  "TwinMux 2nd TS tag (0/1)");
    table->addColumn<int16_t>("rpcBit",  vRpcBit,  "RPC confirmation flag (-10/0/1)");
    table->addColumn<uint8_t>("hasRpc",  vHasRpc,  "1 when DT primitive carries RPC confirmation");
    table->addColumn<uint8_t>("isSecondTs", vIsSecondTs, "1 when primitive is tagged as second time sample");
    ev.put(std::move(table));
  }

private:
  const edm::EDGetTokenT<L1MuDTChambPhContainer> src_;
  const std::string name_;
  const std::string doc_;
};

DEFINE_FWK_MODULE(DTPhiDigiFlatTableProducer);

// ============================================================================
// 2. Ph2DTPhiDigiFlatTableProducer — Phase-2 DT phi primitives
// ============================================================================
class Ph2DTPhiDigiFlatTableProducer : public edm::global::EDProducer<> {
public:
  explicit Ph2DTPhiDigiFlatTableProducer(const edm::ParameterSet& ps)
      : src_(mayConsume<L1Phase2MuDTPhContainer>(ps.getParameter<edm::InputTag>("src"))),
        name_(ps.getParameter<std::string>("name")),
        doc_(ps.getParameter<std::string>("doc")) {
    produces<nanoaod::FlatTable>();
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("src", edm::InputTag("dtTriggerPhase2PrimitiveDigis"));
    desc.add<std::string>("name", "Ph2DTPhiDigi");
    desc.add<std::string>("doc", "Phase-2 DT phi trigger primitives");
    descriptions.addWithDefaultLabel(desc);
  }

  void produce(edm::StreamID, edm::Event& ev, const edm::EventSetup&) const override {
    const auto container = ev.getHandle(src_);
    const auto* digis = (container.isValid()) ? container->getContainer() : nullptr;

    const unsigned int n = digis ? digis->size() : 0;

    std::vector<int16_t> vBx, vWheel, vSector, vStation, vSL;
    // phi and chi2 are stored as int32: the packed phi word (PHIRES_CONV=131072 counts/rad, range
    // up to +-65536 for the full +-0.5 rad segment window) and chi2 both overflow int16_t for
    // segments away from the sector centre / high-chi2 fits, wrapping into bogus negative values.
    // See ROOT_BRANCHES.md section 16.2b for the confirmed overflow measurement.
    std::vector<int32_t> vPhi, vChi2;
    std::vector<int16_t> vPhiBend, vQuality, vIdx, vT0, vRpcFlag;
    std::vector<uint8_t> vHasRpc, vIsHighQuality, vT0Valid;
    vBx.reserve(n); vWheel.reserve(n); vSector.reserve(n);
    vStation.reserve(n); vSL.reserve(n);
    vPhi.reserve(n); vPhiBend.reserve(n); vQuality.reserve(n);
    vIdx.reserve(n); vT0.reserve(n); vChi2.reserve(n); vRpcFlag.reserve(n);
    vHasRpc.reserve(n); vIsHighQuality.reserve(n); vT0Valid.reserve(n);

    if (digis) {
      for (const auto& d : *digis) {
        vBx.push_back(d.bxNum());
        vWheel.push_back(d.whNum());
        vSector.push_back(d.scNum());
        vStation.push_back(d.stNum());
        vSL.push_back(d.slNum());
        vPhi.push_back(static_cast<int32_t>(d.phi()));
        vPhiBend.push_back(d.phiBend());
        vQuality.push_back(d.quality());
        vIdx.push_back(d.index());
        vT0.push_back(static_cast<int16_t>(d.t0()));
        vChi2.push_back(static_cast<int32_t>(d.chi2()));
        vRpcFlag.push_back(d.rpcFlag());
        vHasRpc.push_back(d.rpcFlag() != 0);
        vIsHighQuality.push_back(d.quality() >= 4);
        vT0Valid.push_back(std::abs(static_cast<int>(d.t0())) > 0);
      }
    }

    auto table = std::make_unique<nanoaod::FlatTable>(n, name_, false, false);
    table->setDoc(doc_);
    table->addColumn<int16_t>("bx",       vBx,       "bunch crossing number");
    table->addColumn<int16_t>("wheel",    vWheel,    "DT wheel (-2..+2)");
    table->addColumn<int16_t>("sector",   vSector,   "DT sector (0..11)");
    table->addColumn<int16_t>("station",  vStation,  "DT station (1..4)");
    table->addColumn<int16_t>("sl",       vSL,       "superlayer number (1 or 3 for phi)");
    table->addColumn<int32_t>("phi",      vPhi,      "phi angle [1/131072 rad per unit; full int32, no overflow]");
    table->addColumn<int16_t>("phiBend",  vPhiBend,  "phi bending angle [local direction proxy]");
    table->addColumn<int16_t>("quality",  vQuality,  "segment quality code");
    table->addColumn<int16_t>("index",    vIdx,      "segment index within chamber");
    table->addColumn<int16_t>("t0",       vT0,       "segment t0 timing [ns-scale; displaced muon indicator]");
    table->addColumn<int32_t>("chi2",     vChi2,     "segment fit chi2 [quality/noise rejection; full int32, no overflow]");
    table->addColumn<int16_t>("rpcFlag",  vRpcFlag,  "RPC confirmation flag");
    table->addColumn<uint8_t>("hasRpc",   vHasRpc,   "1 when phase-2 DT segment carries RPC confirmation");
    table->addColumn<uint8_t>("isHighQuality", vIsHighQuality, "1 when DT segment quality >= 4");
    table->addColumn<uint8_t>("t0Valid",  vT0Valid,  "1 if a DT timing (t0) measurement is available for this hit (t0 != 0); does NOT imply the hit is late/displaced, only that a t0 was assigned");
    ev.put(std::move(table));
  }

private:
  const edm::EDGetTokenT<L1Phase2MuDTPhContainer> src_;
  const std::string name_;
  const std::string doc_;
};

DEFINE_FWK_MODULE(Ph2DTPhiDigiFlatTableProducer);

// ============================================================================
// 3. Ph2DTThetaDigiFlatTableProducer — Phase-2 DT theta primitives
// ============================================================================
class Ph2DTThetaDigiFlatTableProducer : public edm::global::EDProducer<> {
public:
  explicit Ph2DTThetaDigiFlatTableProducer(const edm::ParameterSet& ps)
      : src_(mayConsume<L1Phase2MuDTThContainer>(ps.getParameter<edm::InputTag>("src"))),
        name_(ps.getParameter<std::string>("name")),
        doc_(ps.getParameter<std::string>("doc")) {
    produces<nanoaod::FlatTable>();
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("src", edm::InputTag("dtTriggerPhase2PrimitiveDigis"));
    desc.add<std::string>("name", "Ph2DTThDigi");
    desc.add<std::string>("doc", "Phase-2 DT theta trigger primitives");
    descriptions.addWithDefaultLabel(desc);
  }

  void produce(edm::StreamID, edm::Event& ev, const edm::EventSetup&) const override {
    const auto container = ev.getHandle(src_);
    const auto* digis = (container.isValid()) ? container->getContainer() : nullptr;

    const unsigned int n = digis ? digis->size() : 0;

    std::vector<int16_t> vBx, vWheel, vSector, vStation;
    std::vector<int16_t> vZ, vK, vQuality, vIdx, vRpcFlag;
    std::vector<int32_t> vT0, vChi2;
    std::vector<uint8_t> vHasRpc, vIsHighQuality, vT0Valid;
    vBx.reserve(n); vWheel.reserve(n); vSector.reserve(n); vStation.reserve(n);
    vZ.reserve(n); vK.reserve(n); vQuality.reserve(n);
    vIdx.reserve(n); vT0.reserve(n); vChi2.reserve(n); vRpcFlag.reserve(n);
    vHasRpc.reserve(n); vIsHighQuality.reserve(n); vT0Valid.reserve(n);

    if (digis) {
      for (const auto& d : *digis) {
        vBx.push_back(d.bxNum());
        vWheel.push_back(d.whNum());
        vSector.push_back(d.scNum());
        vStation.push_back(d.stNum());
        vZ.push_back(static_cast<int16_t>(d.z()));
        vK.push_back(static_cast<int16_t>(d.k()));
        vQuality.push_back(d.quality());
        vIdx.push_back(d.index());
        vT0.push_back(static_cast<int32_t>(d.t0()));
        vChi2.push_back(static_cast<int32_t>(d.chi2()));
        vRpcFlag.push_back(d.rpcFlag());
        vHasRpc.push_back(d.rpcFlag() != 0);
        vIsHighQuality.push_back(d.quality() >= 4);
        vT0Valid.push_back(std::abs(static_cast<int>(d.t0())) > 0);
      }
    }

    auto table = std::make_unique<nanoaod::FlatTable>(n, name_, false, false);
    table->setDoc(doc_);
    table->addColumn<int16_t>("bx",       vBx,       "bunch crossing number");
    table->addColumn<int16_t>("wheel",    vWheel,    "DT wheel (-2..+2)");
    table->addColumn<int16_t>("sector",   vSector,   "DT sector (0..11)");
    table->addColumn<int16_t>("station",  vStation,  "DT station (1..4)");
    table->addColumn<int16_t>("z",        vZ,        "global z position of theta segment [cm, scale TBD]");
    table->addColumn<int16_t>("k",        vK,        "theta local slope dz/dl [pointing residual variable]");
    table->addColumn<int16_t>("quality",  vQuality,  "segment quality code");
    table->addColumn<int16_t>("index",    vIdx,      "segment index within chamber");
    table->addColumn<int32_t>("t0",       vT0,       "theta segment t0 timing [displaced muon indicator; int32, no truncation]");
    table->addColumn<int32_t>("chi2",     vChi2,     "theta segment fit chi2 [int32, no truncation]");
    table->addColumn<int16_t>("rpcFlag",  vRpcFlag,  "RPC confirmation flag");
    table->addColumn<uint8_t>("hasRpc",   vHasRpc,   "1 when phase-2 DT theta segment carries RPC confirmation");
    table->addColumn<uint8_t>("isHighQuality", vIsHighQuality, "1 when DT theta segment quality >= 4");
    table->addColumn<uint8_t>("t0Valid",  vT0Valid,  "1 if a DT theta timing (t0) measurement is available for this hit (t0 != 0); does NOT imply the hit is late/displaced, only that a t0 was assigned");
    ev.put(std::move(table));
  }

private:
  const edm::EDGetTokenT<L1Phase2MuDTThContainer> src_;
  const std::string name_;
  const std::string doc_;
};

DEFINE_FWK_MODULE(Ph2DTThetaDigiFlatTableProducer);

// ============================================================================
// 4. CSCLCTDigiFlatTableProducer — CSC correlated LCT digis
// ============================================================================
class CSCLCTDigiFlatTableProducer : public edm::global::EDProducer<> {
public:
  explicit CSCLCTDigiFlatTableProducer(const edm::ParameterSet& ps)
      : src_(mayConsume<CSCCorrelatedLCTDigiCollection>(ps.getParameter<edm::InputTag>("src"))),
        cscGeometryToken_(esConsumes<CSCGeometry, MuonGeometryRecord>()),
        name_(ps.getParameter<std::string>("name")),
        doc_(ps.getParameter<std::string>("doc")) {
    produces<nanoaod::FlatTable>();
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("src", edm::InputTag("simCscTriggerPrimitiveDigis", "MPCSORTED"));
    desc.add<std::string>("name", "CSCLctDigi");
    desc.add<std::string>("doc", "CSC correlated LCT trigger primitives");
    descriptions.addWithDefaultLabel(desc);
  }

  void produce(edm::StreamID, edm::Event& ev, const edm::EventSetup& es) const override {
    const auto colHandle = ev.getHandle(src_);
    const CSCGeometry& cscGeometry = es.getData(cscGeometryToken_);

    // Pre-count total digis across all DetSets
    unsigned int n = 0;
    if (colHandle.isValid()) {
      for (const auto& detPair : *colHandle) {
        n += std::distance(detPair.second.first, detPair.second.second);
      }
    }

    // CSCDetId fields
    std::vector<int16_t> vEndcap, vStation, vRing, vChamber;
    // LCT primitive fields
    std::vector<int16_t> vKeywire, vStrip, vPattern, vRun3Pattern;
    std::vector<int16_t> vSlope, vBend, vQuality, vBx, vCscId, vValid;
    std::vector<uint8_t> vQuartStripBit, vEighthStripBit;
    // Geometry-resolved coordinate (producer-side, from key wire group)
    std::vector<float> vEta;

    vEndcap.reserve(n); vStation.reserve(n); vRing.reserve(n); vChamber.reserve(n);
    vKeywire.reserve(n); vStrip.reserve(n); vPattern.reserve(n); vRun3Pattern.reserve(n);
    vSlope.reserve(n); vBend.reserve(n); vQuality.reserve(n);
    vBx.reserve(n); vCscId.reserve(n); vValid.reserve(n);
    vQuartStripBit.reserve(n); vEighthStripBit.reserve(n);
    vEta.reserve(n);

    for (const auto& detPair : *colHandle) {
      const CSCDetId id(detPair.first);
      const CSCChamber* chamber = cscGeometry.chamber(id);
      for (auto lctIt = detPair.second.first; lctIt != detPair.second.second; ++lctIt) {
        const auto& lct = *lctIt;
        vEndcap.push_back(id.endcap());     // 1=+z, 2=-z
        vStation.push_back(id.station());   // 1..4
        vRing.push_back(id.ring());         // 1..4
        vChamber.push_back(id.chamber());   // 1..36
        vKeywire.push_back(lct.getKeyWG());
        vStrip.push_back(lct.getStrip());   // half-strip 0..159
        vPattern.push_back(lct.getPattern());
        vRun3Pattern.push_back(lct.getRun3Pattern());
        vSlope.push_back(lct.getSlope());   // Run-3: 4-bit local slope magnitude
        vBend.push_back(lct.getBend());     // 0=left, 1=right
        vQuality.push_back(lct.getQuality());
        vBx.push_back(lct.getBX());
        vCscId.push_back(lct.getCSCID());
        vValid.push_back(lct.isValid() ? 1 : 0);
        vQuartStripBit.push_back(lct.getQuartStripBit() ? 1 : 0);
        vEighthStripBit.push_back(lct.getEighthStripBit() ? 1 : 0);

        // Geometry-resolved eta from the key wire group, using the same ALCT key layer
        // and localCenterOfWireGroup()->toGlobal() transform used by OmtfAngleConverter::getGlobalEta().
        float eta = 0.f;
        if (chamber) {
          const CSCLayer* keyLayer = chamber->layer(CSCConstants::KEY_ALCT_LAYER);
          if (keyLayer) {
            const LocalPoint lpWg = keyLayer->geometry()->localCenterOfWireGroup(lct.getKeyWG());
            const GlobalPoint gpWg = keyLayer->surface().toGlobal(lpWg);
            eta = gpWg.eta();
          }
        }
        vEta.push_back(eta);
      }
    }

    auto table = std::make_unique<nanoaod::FlatTable>(n, name_, false, false);
    table->setDoc(doc_);
    table->addColumn<int16_t>("endcap",      vEndcap,      "endcap: 1=+z, 2=-z");
    table->addColumn<int16_t>("station",     vStation,     "CSC station (1..4)");
    table->addColumn<int16_t>("ring",        vRing,        "CSC ring (1..4)");
    table->addColumn<int16_t>("chamber",     vChamber,     "CSC chamber (1..36)");
    table->addColumn<int16_t>("valid",       vValid,       "LCT validity flag");
    table->addColumn<int16_t>("quality",     vQuality,     "combined ALCT+CLCT quality (0..15)");
    table->addColumn<int16_t>("keywire",     vKeywire,     "key wire group (z/eta position, 0..111)");
    table->addColumn<int16_t>("strip",       vStrip,       "key half-strip phi position (0..159)");
    table->addColumn<int16_t>("pattern",     vPattern,     "Run-2 LCT pattern ID (1..10, angle class)");
    table->addColumn<int16_t>("run3Pattern", vRun3Pattern, "Run-3 CSC pattern (0..4, angle class)");
    table->addColumn<int16_t>("slope",       vSlope,       "Run-3 local phi slope magnitude [halfstrips/layer, 0..15]");
    table->addColumn<uint8_t>("quartStripBit", vQuartStripBit, "CSC quarter-strip precision bit");
    table->addColumn<uint8_t>("eighthStripBit", vEighthStripBit, "CSC eighth-strip precision bit");
    table->addColumn<int16_t>("bend",        vBend,        "local bend direction: 0=left, 1=right");
    table->addColumn<int16_t>("bx",          vBx,          "bunch crossing number");
    table->addColumn<int16_t>("cscId",       vCscId,       "CSC chamber ID within sector (1..9)");
    table->addColumn<float>("eta", vEta, "geometry-resolved global eta from the key wire group "
                                         "(ALCT key layer, localCenterOfWireGroup->toGlobal)", 12);
    ev.put(std::move(table));
  }

private:
  const edm::EDGetTokenT<CSCCorrelatedLCTDigiCollection> src_;
  const edm::ESGetToken<CSCGeometry, MuonGeometryRecord> cscGeometryToken_;
  const std::string name_;
  const std::string doc_;
};

DEFINE_FWK_MODULE(CSCLCTDigiFlatTableProducer);

// ============================================================================
// 5. RPCDigiFlatTableProducer — RPC strip digis
// ============================================================================
class RPCDigiFlatTableProducer : public edm::global::EDProducer<> {
public:
  explicit RPCDigiFlatTableProducer(const edm::ParameterSet& ps)
      : src_(mayConsume<RPCDigiCollection>(ps.getParameter<edm::InputTag>("src"))),
        rpcGeometryToken_(esConsumes<RPCGeometry, MuonGeometryRecord>()),
        name_(ps.getParameter<std::string>("name")),
        doc_(ps.getParameter<std::string>("doc")),
        maxBxRange_(ps.getParameter<int>("maxBxRange")) {
    produces<nanoaod::FlatTable>();
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("src", edm::InputTag("simMuonRPCDigis"));
    desc.add<std::string>("name", "RPCDigi");
    desc.add<std::string>("doc", "RPC strip digis (strip + BX)");
    desc.add<int>("maxBxRange", 2);  // keep only |bx| <= maxBxRange to limit size
    descriptions.addWithDefaultLabel(desc);
  }

  void produce(edm::StreamID, edm::Event& ev, const edm::EventSetup& es) const override {
    const auto colHandle = ev.getHandle(src_);
    if (!colHandle.isValid()) {
      ev.put(std::make_unique<nanoaod::FlatTable>(0, name_, false, false));
      return;
    }
    const auto& collection = *colHandle;
    const RPCGeometry& rpcGeometry = es.getData(rpcGeometryToken_);

    // RPCDetId fields
    std::vector<int16_t> vRegion, vRing, vStation, vSector, vLayer, vSubsector, vRoll;
    // RPC primitive fields
    std::vector<int16_t> vStrip, vBx;
    // Derived RPC cluster observables
    std::vector<int16_t> vClusterSize, vClusterStripSpan, vRollBxSpan;
    std::vector<uint8_t> vIsIsolated;
    // Geometry-resolved coordinates (producer-side, from strip -> roll geometry)
    std::vector<float> vEta, vPhi;

    for (const auto& detPair : collection) {
      const RPCDetId id(detPair.first);
      const RPCRoll* roll = rpcGeometry.roll(id);
      struct DigiRow {
        int16_t strip;
        int16_t bx;
      };
      std::vector<DigiRow> rows;
      rows.reserve(std::distance(detPair.second.first, detPair.second.second));

      int16_t minBxInRoll = 32767;
      int16_t maxBxInRoll = -32768;
      for (auto digiIt = detPair.second.first; digiIt != detPair.second.second; ++digiIt) {
        const auto& digi = *digiIt;
        if (std::abs(digi.bx()) > maxBxRange_) continue;
        rows.push_back({static_cast<int16_t>(digi.strip()), static_cast<int16_t>(digi.bx())});
        minBxInRoll = std::min(minBxInRoll, static_cast<int16_t>(digi.bx()));
        maxBxInRoll = std::max(maxBxInRoll, static_cast<int16_t>(digi.bx()));
      }

      if (rows.empty()) continue;

      std::vector<int16_t> clusterSize(rows.size(), 1);
      std::vector<int16_t> clusterStripSpan(rows.size(), 0);

      std::map<int16_t, std::vector<size_t>> idxByBx;
      for (size_t i = 0; i < rows.size(); ++i) {
        idxByBx[rows[i].bx].push_back(i);
      }

      for (auto& kv : idxByBx) {
        auto& idxs = kv.second;
        std::sort(idxs.begin(), idxs.end(), [&](size_t a, size_t b) {
          return rows[a].strip < rows[b].strip;
        });

        size_t blockStart = 0;
        while (blockStart < idxs.size()) {
          size_t blockEnd = blockStart;
          int16_t prevStrip = rows[idxs[blockStart]].strip;
          while (blockEnd + 1 < idxs.size()) {
            const int16_t nextStrip = rows[idxs[blockEnd + 1]].strip;
            if (nextStrip <= prevStrip + 1) {
              prevStrip = nextStrip;
              ++blockEnd;
            } else {
              break;
            }
          }

          const int16_t firstStrip = rows[idxs[blockStart]].strip;
          const int16_t lastStrip = rows[idxs[blockEnd]].strip;
          const int16_t size = static_cast<int16_t>(blockEnd - blockStart + 1);
          const int16_t span = static_cast<int16_t>(lastStrip - firstStrip);
          for (size_t j = blockStart; j <= blockEnd; ++j) {
            clusterSize[idxs[j]] = size;
            clusterStripSpan[idxs[j]] = span;
          }
          blockStart = blockEnd + 1;
        }
      }

      const int16_t rollBxSpan = static_cast<int16_t>(maxBxInRoll - minBxInRoll);
      for (size_t i = 0; i < rows.size(); ++i) {
        vRegion.push_back(id.region());     // 0=barrel, +1=+endcap, -1=-endcap
        vRing.push_back(id.ring());         // barrel wheel or endcap ring
        vStation.push_back(id.station());   // 1..4
        vSector.push_back(id.sector());     // 1..12
        vLayer.push_back(id.layer());       // 1..2
        vSubsector.push_back(id.subsector());
        vRoll.push_back(id.roll());         // eta partition (roll) within chamber
        vStrip.push_back(rows[i].strip);    // strip number
        vBx.push_back(rows[i].bx);          // bunch crossing [timing variable]
        vClusterSize.push_back(clusterSize[i]);
        vClusterStripSpan.push_back(clusterStripSpan[i]);
        vIsIsolated.push_back(clusterSize[i] == 1 ? 1 : 0);
        vRollBxSpan.push_back(rollBxSpan);

        // Geometry-resolved eta/phi from the strip center, via RPCRoll::centreOfStrip->toGlobal.
        float eta = 0.f, phi = 0.f;
        if (roll && rows[i].strip >= 1 && rows[i].strip <= roll->nstrips()) {
          const GlobalPoint gp = roll->toGlobal(roll->centreOfStrip(rows[i].strip));
          eta = gp.eta();
          phi = gp.phi();
        }
        vEta.push_back(eta);
        vPhi.push_back(phi);
      }
    }

    const unsigned int n = vStrip.size();
    auto table = std::make_unique<nanoaod::FlatTable>(n, name_, false, false);
    table->setDoc(doc_);
    table->addColumn<int16_t>("region",     vRegion,     "region: 0=barrel, +1=+endcap, -1=-endcap");
    table->addColumn<int16_t>("ring",       vRing,       "barrel wheel or endcap ring");
    table->addColumn<int16_t>("station",    vStation,    "station (1..4)");
    table->addColumn<int16_t>("sector",     vSector,     "sector (1..12)");
    table->addColumn<int16_t>("layer",      vLayer,      "layer (1..2)");
    table->addColumn<int16_t>("subsector",  vSubsector,  "subsector");
    table->addColumn<int16_t>("roll",       vRoll,       "roll = eta partition within chamber");
    table->addColumn<int16_t>("strip",      vStrip,      "strip number (phi position in roll)");
    table->addColumn<int16_t>("bx",         vBx,         "bunch crossing [timing: late BX -> displaced]");
    table->addColumn<int16_t>("clusterSize", vClusterSize, "contiguous-strip cluster size at fixed DetId and BX");
    table->addColumn<int16_t>("clusterStripSpan", vClusterStripSpan, "cluster strip span = maxStrip-minStrip at fixed DetId and BX");
    table->addColumn<uint8_t>("isIsolated", vIsIsolated, "1 for single-strip cluster at fixed DetId and BX");
    table->addColumn<int16_t>("rollBxSpan", vRollBxSpan, "BX span in this roll after BX filtering (maxBX-minBX)");
    table->addColumn<float>("eta", vEta, "geometry-resolved global eta from the strip center (RPCRoll::centreOfStrip->toGlobal)", 12);
    table->addColumn<float>("phi", vPhi, "geometry-resolved global phi [rad] from the strip center (RPCRoll::centreOfStrip->toGlobal)", 12);
    ev.put(std::move(table));
  }

private:
  const edm::EDGetTokenT<RPCDigiCollection> src_;
  const edm::ESGetToken<RPCGeometry, MuonGeometryRecord> rpcGeometryToken_;
  const std::string name_;
  const std::string doc_;
  const int maxBxRange_;
};

DEFINE_FWK_MODULE(RPCDigiFlatTableProducer);
