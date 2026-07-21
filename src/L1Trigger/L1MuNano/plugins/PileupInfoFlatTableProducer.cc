#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "DataFormats/NanoAOD/interface/FlatTable.h"
#include "SimDataFormats/PileupSummaryInfo/interface/PileupSummaryInfo.h"

#include <string>
#include <vector>

class PileupInfoFlatTableProducer : public edm::global::EDProducer<> {
public:
  explicit PileupInfoFlatTableProducer(const edm::ParameterSet& ps)
      : src_(mayConsume<std::vector<PileupSummaryInfo>>(ps.getParameter<edm::InputTag>("src"))),
        name_(ps.getParameter<std::string>("name")),
        doc_(ps.getParameter<std::string>("doc")) {
    produces<nanoaod::FlatTable>();
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("src", edm::InputTag("addPileupInfo"));
    desc.add<std::string>("name", "Pileup");
    desc.add<std::string>("doc", "Event-level pileup summary from addPileupInfo");
    descriptions.addWithDefaultLabel(desc);
  }

  void produce(edm::StreamID, edm::Event& ev, const edm::EventSetup&) const override {
    const auto puHandle = ev.getHandle(src_);

    int16_t nPU = -1;
    int16_t nPUm1 = -1;
    int16_t nPUp1 = -1;
    float nTrueInt = -1.f;

    if (puHandle.isValid()) {
      for (const auto& pu : *puHandle) {
        const int bx = pu.getBunchCrossing();
        const int interactions = pu.getPU_NumInteractions();
        if (bx == 0) {
          nPU = static_cast<int16_t>(interactions);
          nTrueInt = pu.getTrueNumInteractions();
        } else if (bx == -1) {
          nPUm1 = static_cast<int16_t>(interactions);
        } else if (bx == 1) {
          nPUp1 = static_cast<int16_t>(interactions);
        }
      }
    }

    std::vector<int16_t> vNPU{nPU};
    std::vector<float> vNTrueInt{nTrueInt};
    std::vector<int16_t> vNPUm1{nPUm1};
    std::vector<int16_t> vNPUp1{nPUp1};

    auto table = std::make_unique<nanoaod::FlatTable>(1, name_, true, false);
    table->setDoc(doc_);
    table->addColumn<int16_t>("nPU", vNPU, "in-time pileup interactions (BX=0)");
    table->addColumn<float>("nTrueInt", vNTrueInt, "true in-time pileup interactions (BX=0)", 8);
    table->addColumn<int16_t>("nPUm1", vNPUm1, "pileup interactions in previous bunch crossing (BX=-1)");
    table->addColumn<int16_t>("nPUp1", vNPUp1, "pileup interactions in next bunch crossing (BX=+1)");
    ev.put(std::move(table));
  }

private:
  const edm::EDGetTokenT<std::vector<PileupSummaryInfo>> src_;
  const std::string name_;
  const std::string doc_;
};

DEFINE_FWK_MODULE(PileupInfoFlatTableProducer);
