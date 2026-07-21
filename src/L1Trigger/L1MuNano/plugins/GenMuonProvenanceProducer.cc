/*
 * GenMuonProvenanceProducer.cc
 *
 * Produces provenance ValueMaps for generator particles, keyed to the input
 * genParticles collection:
 *   - parentPdgId      : immediate mother PDG id (0 if unavailable)
 *   - isDecayInFlight  : 1 if first non-muon ancestor is pi/K (charged/neutral)
 *                        0 otherwise
 *   - provenanceStatus : lXY-consistent provenance classification:
 *                          0 = prompt          (lXY < promptLxyCm_, produced at/near the primary vertex)
 *                          1 = decayInFlight    (lXY >= promptLxyCm_ AND first non-muon ancestor is pi/K)
 *                          2 = otherDisplaced   (lXY >= promptLxyCm_ but ancestor is not pi/K, e.g. heavy
 *                                                flavor decay or an exotic/LLP parent)
 *                        This gives the DIF policy a label that is consistent with the operational
 *                        dxy-in-prompt-gun proxy instead of relying on pdgId ancestry alone.
 *   - tpMuonIdx        : OPTIONAL reciprocal index into the selected TpMuon table (see
 *                        DataROOTDumper2AllInput.cc's TpMuon_genMuonIdx, which is the inverse of this
 *                        map). -1 if disabled (no 'trackingParticles' InputTag configured, or this
 *                        genParticle row has no matching selected TrackingParticle). Only produced when
 *                        the 'trackingParticles' parameter is a non-empty InputTag. Uses the IDENTICAL
 *                        TrackingParticle selection (|pdgId|==13, pt>1.5 GeV, |eta|<1.6 -- 
 *                        kTpSelectionVersion=1) and EXACT tp.genParticles()[0] Ref lookup (not
 *                        kinematic dR matching) as DataROOTDumper2AllInput.cc, and the SAME
 *                        TrackingParticle collection iteration order to determine the TpMuon row index,
 *                        so the two independently-computed indices are consistent with each other.
 *                        On conflict (two selected TPs referencing the same genParticle row), the first
 *                        TP encountered in collection order wins; this is a known limitation.
 *
 * These maps are attached to the GenMuon Nano table via ExtVar.
 */

#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/Utilities/interface/StreamID.h"

#include "DataFormats/HepMCCandidate/interface/GenParticle.h"
#include "DataFormats/HepMCCandidate/interface/GenParticleFwd.h"
#include "DataFormats/Common/interface/ValueMap.h"
#include "SimDataFormats/TrackingAnalysis/interface/TrackingParticle.h"
#include "SimDataFormats/TrackingAnalysis/interface/TrackingParticleFwd.h"

#include <vector>
#include <cmath>

namespace {
bool isDifParentPdgId(const int pdgIdAbs) {
  return pdgIdAbs == 211 || pdgIdAbs == 321 || pdgIdAbs == 130 || pdgIdAbs == 310;
}

enum ProvenanceStatus : int { kPrompt = 0, kDecayInFlight = 1, kOtherDisplaced = 2 };

// kTpSelectionVersion=1 -- must stay identical to DataROOTDumper2AllInput.cc's TpMuon selection cut.
bool passesTpMuonSelection(const TrackingParticle& tp) {
  return std::abs(tp.pdgId()) == 13 && tp.pt() > 1.5 && std::abs(tp.momentum().eta()) < 1.6;
}
}  // namespace

class GenMuonProvenanceProducer : public edm::global::EDProducer<> {
public:
  explicit GenMuonProvenanceProducer(const edm::ParameterSet& cfg)
      : srcToken_(consumes<reco::GenParticleCollection>(cfg.getParameter<edm::InputTag>("src"))),
        promptLxyCm_(cfg.getParameter<double>("promptLxyCm")),
        tpTag_(cfg.getParameter<edm::InputTag>("trackingParticles")),
        tpEnabled_(!tpTag_.label().empty()) {
    produces<edm::ValueMap<int>>("parentPdgId");
    produces<edm::ValueMap<int>>("isDecayInFlight");
    produces<edm::ValueMap<int>>("provenanceStatus");
    produces<edm::ValueMap<int>>("tpMuonIdx");
    if (tpEnabled_)
      tpToken_ = mayConsume<TrackingParticleCollection>(tpTag_);
  }

  ~GenMuonProvenanceProducer() override = default;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("src", edm::InputTag("genParticles"));
    desc.add<double>("promptLxyCm", 0.01)
        ->setComment("lXY threshold [cm] below which a gen muon is classified as prompt in provenanceStatus");
    desc.add<edm::InputTag>("trackingParticles", edm::InputTag(""))
        ->setComment(
            "Optional TrackingParticle collection (e.g. mix:MergedTrackTruth) used to fill tpMuonIdx. "
            "Leave empty (default) to disable tpMuonIdx (all entries -1).");
    descriptions.add("GenMuonProvenanceProducer", desc);
  }

private:
  void produce(edm::StreamID, edm::Event& event, const edm::EventSetup&) const override {
    const auto genParticles = event.getHandle(srcToken_);
    const size_t n = genParticles->size();

    std::vector<int> parentPdgId(n, 0);
    std::vector<int> isDecayInFlight(n, 0);
    std::vector<int> provenanceStatus(n, kPrompt);

    for (size_t i = 0; i < n; ++i) {
      const reco::GenParticle& gp = (*genParticles)[i];

      const reco::Candidate* mother = (gp.numberOfMothers() > 0) ? gp.mother(0) : nullptr;
      parentPdgId[i] = mother ? mother->pdgId() : 0;

      const reco::Candidate* ancestor = mother;
      int guard = 0;
      while (ancestor && std::abs(ancestor->pdgId()) == 13 && guard < 20) {
        ancestor = (ancestor->numberOfMothers() > 0) ? ancestor->mother(0) : nullptr;
        ++guard;
      }

      if (ancestor) {
        isDecayInFlight[i] = isDifParentPdgId(std::abs(ancestor->pdgId())) ? 1 : 0;
      }

      const double lXY = std::hypot(gp.vx(), gp.vy());
      if (lXY < promptLxyCm_) {
        provenanceStatus[i] = kPrompt;
      } else if (isDecayInFlight[i] == 1) {
        provenanceStatus[i] = kDecayInFlight;
      } else {
        provenanceStatus[i] = kOtherDisplaced;
      }
    }

    auto putMap = [&](std::vector<int>& vals, const std::string& label) {
      auto vm = std::make_unique<edm::ValueMap<int>>();
      edm::ValueMap<int>::Filler filler(*vm);
      filler.insert(genParticles, vals.begin(), vals.end());
      filler.fill();
      event.put(std::move(vm), label);
    };

    putMap(parentPdgId, "parentPdgId");
    putMap(isDecayInFlight, "isDecayInFlight");
    putMap(provenanceStatus, "provenanceStatus");

    // tpMuonIdx: reciprocal of DataROOTDumper2AllInput.cc's TpMuon_genMuonIdx (see header doc).
    std::vector<int> tpMuonIdx(n, -1);
    if (tpEnabled_) {
      auto tpHandle = event.getHandle(tpToken_);
      if (tpHandle.isValid()) {
        int tpRow = 0;
        for (const auto& tp : *tpHandle) {
          if (!passesTpMuonSelection(tp)) {
            continue;
          }
          const int thisTpRow = tpRow++;
          if (!tp.genParticles().empty()) {
            const auto& ref = tp.genParticles()[0];
            if (ref.isNonnull() && ref.id() == genParticles.id()) {
              const size_t idx = ref.key();
              if (idx < n && tpMuonIdx[idx] < 0) {
                tpMuonIdx[idx] = thisTpRow;
              }
            }
          }
        }
      }
    }
    putMap(tpMuonIdx, "tpMuonIdx");
  }

  const edm::EDGetTokenT<reco::GenParticleCollection> srcToken_;
  const double promptLxyCm_;
  const edm::InputTag tpTag_;
  const bool tpEnabled_;
  edm::EDGetTokenT<TrackingParticleCollection> tpToken_;
};

DEFINE_FWK_MODULE(GenMuonProvenanceProducer);

