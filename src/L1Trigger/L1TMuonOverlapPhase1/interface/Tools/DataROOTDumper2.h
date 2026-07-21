/*
 * DataROOTDumper2.h
 *
 *  Created on: Dec 11, 2019
 *      Author: kbunkow
 */

#ifndef L1T_OmtfP1_TOOLS_DATAROOTDUMPER2_H_
#define L1T_OmtfP1_TOOLS_DATAROOTDUMPER2_H_

#include "L1Trigger/L1TMuonOverlapPhase1/interface/Tools/EmulationObserverBase.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Tools/CandidateSimMuonMatcher.h"

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "SimDataFormats/Track/interface/SimTrackContainer.h"

#include "TMap.h"
#include "TArrayI.h"
#include "TFile.h"
#include "TH2.h"

#include <functional>
#include <cstdint>
#include <map>
#include <tuple>

class TTree;

struct OmtfEvent {
public:
  uint32_t runNum = 0;
  uint32_t lumiNum = 0;
  unsigned int eventNum = 0;
  uint64_t eventNum64 = 0;

  //muonPt = 0 means that no muon was matched to the candidate
  int32_t muonEvent = -1;
  int16_t muonBunchCrossing = 0;

  //Explicit "is this a gen-matched entry?" flag. All muon* / vertex* / deltaEta / deltaPhi fields above
  //are optional gen-truth-match fields: when hasGenMatch == false they hold their "no match" sentinel
  //values (muonEvent=-1, muonPt=0, etc.) and the row still corresponds to a real OMTF candidate (or, in
  //samples with no truth source configured at all, to every OMTF candidate in the event). This makes
  //fake/ghost studies possible on samples where, by construction, nothing is gen-matched (e.g. pure
  //background/noise productions) since such candidates are no longer silently dropped.
  bool hasGenMatch = false;

  float muonPt = 0, muonEta = 0, muonPhi = 0, muonPropEta = 0, muonPropPhi = 0;
  char muonCharge = 0;
  float muonDxy = 0;
  float muonRho = 0;
  //pdgId in principle should be int
  short parentPdgId = 0;
  float vertexEta = 0;
  float vertexPhi = 0;

  float omtfPt = 0, omtfEta = 0, omtfPhi = 0, omtfUPt = 0;
  char omtfCharge = 0;
  char omtfProcessor = 0;
  short omtfScore = 0;
  short omtfRefHitPhi = 0;

  short omtfHwEta = 0;

  char omtfQuality = 0;
  char omtfRefLayer = 0;
  char omtfRefHitNum = 0;

  unsigned int omtfFiredLayers = 0;

  // Explicit endcap label for the OMTF candidate itself (independent of any gen/TP truth match
  // above): +1 = OMTF_POS, -1 = OMTF_NEG, 0 = unknown/other track-finder type.
  signed char omtfEndcap = 0;
  // BX of the dumped OMTF candidate. This offline per-event dumper only ever emulates/dumps the
  // BX=0 window (there is no multi-BX final-candidate loop here, unlike the online GMT output
  // captured separately in the NanoAOD OMTF_* branches), so this is 0 by construction today. Kept
  // as an explicit branch (rather than only documented) so downstream code does not have to
  // assume it, and so a future multi-BX extension of this dumper is schema-compatible.
  signed char omtfBx = 0;
  // Unique identifier for this dumped OMTF candidate within (run,lumi,event64,omtfEndcap,
  // omtfProcessor,omtfBx): 0,1,2... in the order candidates were produced for that key this
  // event. Lets OMTFHitsTree rows for the same physical candidate be associated unambiguously
  // (e.g. a killed candidate and its surviving neighbor) without relying on row adjacency.
  int candidateId = 0;

  bool killed = false;

  float deltaPhi = 0, deltaEta = 0;

  //float omtfPtCont = 0;

  struct Hit {
    union {
      unsigned long rawData = 0;

      struct {
        char layer;
        char quality;
        char etaHw;
        char valid;
        short deltaR;  // stub_r - refStub_r  [cm]
        short phiDist; // stub_phi - refStub_phi  [HW units]; phiBHw for bending layers
      };
    };

    ~Hit() {}
  };

  std::vector<unsigned long> hits;

  // Extended per-hit branches — parallel to 'hits' (same index i → same stub)
  // These carry the fields that do not fit in the 64-bit Hit union.
  std::vector<short> hits_phiBHw;  // DT bending angle in HW units; 0 for non-DT layers
  std::vector<short> hits_phiHw;   // absolute stub phi in HW units (before ref-subtraction)
  std::vector<short> hits_r;       // absolute radial distance [cm]
  std::vector<signed char> hits_type;  // MuonStub::Type enum cast to int8
  std::vector<signed char> hits_bx;    // bunch crossing offset from BX=0
  // Parallel to 'hits'/'hits_type' above: stable per-primitive identity, mirroring
  // OMTFAllInputTree's reg_stub_primitiveId/layer/quality/etaHw/detId (item 2/3), so a
  // per-candidate stub in OMTFHitsTree can be joined back to its all-input row.
  std::vector<unsigned long long> hits_primitiveId;
  std::vector<signed char> hits_layer;   // logic layer index (redundant with packed Hit::layer,
                                         // but int8 without the 4-bit truncation of the union)
  std::vector<signed char> hits_quality; // qualityHw (redundant with packed Hit::quality, same reason)
  std::vector<signed char> hits_etaHw;   // eta HW (redundant with packed Hit::etaHw, same reason)
  std::vector<uint8_t>     hits_valid;   // 1 if this stub was actually used/valid in the gpResult
  std::vector<uint32_t>    hits_detId;   // raw source chamber/roll DetId
};

class DataROOTDumper2 : public EmulationObserverBase {
public:
  DataROOTDumper2(const edm::ParameterSet& edmCfg,
                  const OMTFConfiguration* omtfConfig,
                  CandidateSimMuonMatcher* candidateSimMuonMatcher);

  ~DataROOTDumper2() override;

  void observeProcesorEmulation(unsigned int iProcessor,
                                l1t::tftype mtfType,
                                const std::shared_ptr<OMTFinput>&,
                                const AlgoMuons& algoCandidates,
                                const AlgoMuons& gbCandidates,
                                const FinalMuons& finalMuons) override;

  void observeEventEnd(const edm::Event& iEvent,
                       std::unique_ptr<l1t::RegionalMuonCandBxCollection>& finalCandidates) override;

  void endJob() override;

protected:
  CandidateSimMuonMatcher* matcher() const { return candidateSimMuonMatcher; }

private:
  void initializeTTree();

  CandidateSimMuonMatcher* candidateSimMuonMatcher = nullptr;

  TTree* rootTree = nullptr;

  OmtfEvent omtfEvent;

  // candidateId assignment (see OmtfEvent::candidateId doc): reset at the start of every event,
  // keyed by (endcap,processor,bx); value is the next id to hand out for that key this event.
  std::map<std::tuple<signed char, char, signed char>, int> nextCandidateIdForKey_;

  unsigned int evntCnt = 0;

  TH1I* ptGenPos = nullptr;
  TH1I* ptGenNeg = nullptr;

  //std::vector<TH2*> hitVsPt;

  bool dumpKilledOmtfCands = false;
};

#endif /* L1T_OmtfP1_TOOLS_DATAROOTDUMPER2_H_ */
