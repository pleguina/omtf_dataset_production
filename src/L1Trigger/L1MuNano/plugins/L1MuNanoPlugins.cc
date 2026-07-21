// L1MuNanoPlugins.cc
// Instantiates NanoAOD FlatTable producers for Phase-2 L1 muon trigger objects.
//
// Plugins registered here:
//   SimpleOMTFTrackCandidateFlatTableProducer
//     = BXVectorSimpleFlatTableProducer<l1t::RegionalMuonCand>
//     reads BXVector<l1t::RegionalMuonCand> from simOmtfPhase2Digis:OMTF
//
//   SimpleMuonStubFlatTableProducer
//     = SimpleFlatTableProducer<l1t::MuonStub>
//     reads l1t::MuonStubCollection from l1tStubsGmt:tps or l1tStubsGmt:kmtf
//     (hybrid stubs with coord1/coord2/eta1/eta2, input format expected by GMT)
//
// Note: SimpleGenParticleFlatTableProducer is already in PhysicsTools/NanoAOD
//       (SimpleFlatTableProducerPlugins.cc) — no need to re-register it.

#include "PhysicsTools/NanoAOD/interface/SimpleFlatTableProducer.h"

#include "DataFormats/L1TMuon/interface/RegionalMuonCand.h"
typedef BXVectorSimpleFlatTableProducer<l1t::RegionalMuonCand>
    SimpleOMTFTrackCandidateFlatTableProducer;

#include "DataFormats/L1TMuonPhase2/interface/MuonStub.h"
typedef SimpleFlatTableProducer<l1t::MuonStub>
    SimpleMuonStubFlatTableProducer;

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(SimpleOMTFTrackCandidateFlatTableProducer);
DEFINE_FWK_MODULE(SimpleMuonStubFlatTableProducer);
