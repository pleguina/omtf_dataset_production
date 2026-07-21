/*
 * GenParticlePropagator.cc
 *
 * EDProducer that propagates generator-level muons (reco::GenParticle,
 * |pdgId|==13, status==1) to the 1st and 2nd muon stations using the
 * SteppingHelixPropagator, and produces ValueMap<float> collections:
 *
 *   <moduleLabel>:etaSt1  — eta at 1st muon station (MB1/ME1)
 *   <moduleLabel>:phiSt1  — phi at 1st muon station
 *   <moduleLabel>:etaSt2  — eta at 2nd muon station (MB2/ME2)
 *   <moduleLabel>:phiSt2  — phi at 2nd muon station
 *
 * The ValueMaps are indexed over the full source GenParticle collection.
 * Non-muon or non-stable particles are filled with -9.0 (sentinel for
 * "propagation not attempted"). Muons for which propagation fails (e.g.
 * outside acceptance) are also filled with -9.0.
 *
 * Based on: https://github.com/folguera/cmssw/blob/from-CMSSW_15_1_0_pre4_L1NanoGenParticlePropagator/
 *   DPGAnalysis/Phase2L1TNanoAOD/plugins/GenParticlePropagator.cc
 * Adapted for CMSSW_14_2_0_pre2 / GEN-SIM workflow using genParticles
 * (not finalGenParticles) and status==1 stable-particle filter.
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

#include "MuonAnalysis/MuonAssociators/interface/PropagateToMuon.h"
#include "MuonAnalysis/MuonAssociators/interface/PropagateToMuonSetup.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateOnSurface.h"

#include <vector>
#include <cmath>

class GenParticlePropagator : public edm::global::EDProducer<> {
public:
  explicit GenParticlePropagator(const edm::ParameterSet&);
  ~GenParticlePropagator() override = default;

  static void fillDescriptions(edm::ConfigurationDescriptions&);

private:
  void produce(edm::StreamID, edm::Event&, const edm::EventSetup&) const override;

  const edm::EDGetTokenT<reco::GenParticleCollection> srcToken_;
  const PropagateToMuonSetup muPropSetup1st_;  // → 1st station (MB1/ME1)
  const PropagateToMuonSetup muPropSetup2nd_;  // → 2nd station (MB2/ME2)
};

// ---------------------------------------------------------------------------
GenParticlePropagator::GenParticlePropagator(const edm::ParameterSet& iConfig)
    : srcToken_(consumes<reco::GenParticleCollection>(iConfig.getParameter<edm::InputTag>("src"))),
      muPropSetup1st_(iConfig.getParameter<edm::ParameterSet>("muProp1st"), consumesCollector()),
      muPropSetup2nd_(iConfig.getParameter<edm::ParameterSet>("muProp2nd"), consumesCollector()) {
  produces<edm::ValueMap<float>>("etaSt1");
  produces<edm::ValueMap<float>>("phiSt1");
  produces<edm::ValueMap<float>>("etaSt2");
  produces<edm::ValueMap<float>>("phiSt2");
}

// ---------------------------------------------------------------------------
void GenParticlePropagator::produce(edm::StreamID,
                                     edm::Event& iEvent,
                                     const edm::EventSetup& iSetup) const {
  const auto genParticles = iEvent.getHandle(srcToken_);

  // Initialise propagators for this event (thread-safe: returns by value)
  PropagateToMuon muProp1 = muPropSetup1st_.init(iSetup);
  PropagateToMuon muProp2 = muPropSetup2nd_.init(iSetup);

  const size_t n = genParticles->size();
  // Pre-fill with sentinel value (-9) for non-muon / non-stable / failed propagation
  std::vector<float> etaSt1(n, -9.f), phiSt1(n, -9.f);
  std::vector<float> etaSt2(n, -9.f), phiSt2(n, -9.f);

  for (size_t i = 0; i < n; ++i) {
    const reco::GenParticle& gp = (*genParticles)[i];

    // Only propagate stable muons (status==1, |pdgId|==13)
    if (std::abs(gp.pdgId()) != 13 || gp.status() != 1)
      continue;

    // --- Station 1 (MB1 barrel / ME1 endcap) ---
    const TrajectoryStateOnSurface tsos1 = muProp1.extrapolate(gp);
    if (tsos1.isValid()) {
      etaSt1[i] = tsos1.globalPosition().eta();
      phiSt1[i] = tsos1.globalPosition().phi();
    }

    // --- Station 2 (MB2 barrel / ME2 endcap) ---
    const TrajectoryStateOnSurface tsos2 = muProp2.extrapolate(gp);
    if (tsos2.isValid()) {
      etaSt2[i] = tsos2.globalPosition().eta();
      phiSt2[i] = tsos2.globalPosition().phi();
    }
  }

  // Package results as ValueMaps keyed to the source collection
  auto makeMap = [&](std::vector<float>& vals, const std::string& label) {
    auto vm = std::make_unique<edm::ValueMap<float>>();
    edm::ValueMap<float>::Filler filler(*vm);
    filler.insert(genParticles, vals.begin(), vals.end());
    filler.fill();
    iEvent.put(std::move(vm), label);
  };

  makeMap(etaSt1, "etaSt1");
  makeMap(phiSt1, "phiSt1");
  makeMap(etaSt2, "etaSt2");
  makeMap(phiSt2, "phiSt2");
}

// ---------------------------------------------------------------------------
void GenParticlePropagator::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("src", edm::InputTag("genParticles"));

  // Propagation to 1st muon station (MB1 barrel / ME1 endcap)
  // useTrack="none" → build starting state from gen-particle vertex + momentum
  // useStation2=false → target 1st station surface
  edm::ParameterSetDescription muProp1st;
  muProp1st.add<std::string>("useTrack",                    "none");
  muProp1st.add<std::string>("useState",                    "atVertex");
  muProp1st.add<bool>("useSimpleGeometry",                  true);
  muProp1st.add<bool>("useStation2",                        false);
  muProp1st.add<bool>("fallbackToME1",                      false);
  muProp1st.add<bool>("cosmicPropagationHypothesis",        false);
  muProp1st.add<bool>("useMB2InOverlap",                    false);
  muProp1st.add<edm::ESInputTag>("propagatorAlong",    edm::ESInputTag("", "SteppingHelixPropagatorAlong"));
  muProp1st.add<edm::ESInputTag>("propagatorAny",      edm::ESInputTag("", "SteppingHelixPropagatorAny"));
  muProp1st.add<edm::ESInputTag>("propagatorOpposite", edm::ESInputTag("", "SteppingHelixPropagatorOpposite"));
  desc.add<edm::ParameterSetDescription>("muProp1st", muProp1st);

  // Propagation to 2nd muon station (MB2 barrel / ME2 endcap)
  // useSimpleGeometry=false → use full muon geometry for better accuracy
  // useStation2=true → target 2nd station surface
  // useMB2InOverlap=true → use MB2 in the barrel-endcap overlap region
  edm::ParameterSetDescription muProp2nd;
  muProp2nd.add<std::string>("useTrack",                    "none");
  muProp2nd.add<std::string>("useState",                    "atVertex");
  muProp2nd.add<bool>("useSimpleGeometry",                  false);
  muProp2nd.add<bool>("useStation2",                        true);
  muProp2nd.add<bool>("fallbackToME1",                      false);
  muProp2nd.add<bool>("cosmicPropagationHypothesis",        false);
  muProp2nd.add<bool>("useMB2InOverlap",                    true);
  muProp2nd.add<edm::ESInputTag>("propagatorAlong",    edm::ESInputTag("", "SteppingHelixPropagatorAlong"));
  muProp2nd.add<edm::ESInputTag>("propagatorAny",      edm::ESInputTag("", "SteppingHelixPropagatorAny"));
  muProp2nd.add<edm::ESInputTag>("propagatorOpposite", edm::ESInputTag("", "SteppingHelixPropagatorOpposite"));
  desc.add<edm::ParameterSetDescription>("muProp2nd", muProp2nd);

  descriptions.add("GenParticlePropagator", desc);
}

DEFINE_FWK_MODULE(GenParticlePropagator);
