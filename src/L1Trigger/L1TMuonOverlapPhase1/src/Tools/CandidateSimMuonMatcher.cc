/*
 * CandidateSimMuonMatcher.cc
 *
 *  Created on: Dec 14, 2020
 *      Author: kbunkow
 */

#include "L1Trigger/L1TMuonOverlapPhase1/interface/Tools/CandidateSimMuonMatcher.h"
#include "L1Trigger/L1TMuon/interface/MicroGMTConfiguration.h"

#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"

#include "DataFormats/GeometrySurface/interface/BoundCylinder.h"
#include "DataFormats/GeometrySurface/interface/SimpleCylinderBounds.h"
#include "DataFormats/GeometrySurface/interface/BoundDisk.h"
#include "DataFormats/GeometrySurface/interface/SimpleDiskBounds.h"
#include "DataFormats/MuonDetId/interface/RPCDetId.h"

#include "TrackingTools/TrajectoryParametrization/interface/GlobalTrajectoryParameters.h"

#include "boost/dynamic_bitset.hpp"

#include "TFile.h"
#include "TH1D.h"

#include <cmath>

double hwGmtPhiToGlobalPhi(int phi) {
  double phiGmtUnit = 2. * M_PI / 576.;
  double globalPhi = phi * phiGmtUnit;

  if (globalPhi > M_PI)
    globalPhi = globalPhi - (2. * M_PI);

  return globalPhi;
}

double foldPhi(double phi) {
  if (phi > M_PI)
    return (phi - 2 * M_PI);
  else if (phi < -M_PI)
    return (phi + 2 * M_PI);

  return phi;
}

MatchingResult::MatchingResult(const SimTrack& simTrack, const SimVertex* simVertex)
    : simTrack(&simTrack), simVertex(simVertex) {
  pdgId = simTrack.type();
  genPt = simTrack.momentum().pt();
  genEta = simTrack.momentum().eta();
  genPhi = simTrack.momentum().phi();
  genCharge = simTrack.charge();

  const math::XYZTLorentzVectorD& vtxPos = this->simVertex->position();
  // NOTE: dxy must be recovered from the *curved* helix, not a straight-line
  // projection (-vx*py+vy*px)/pt (that formula is only accurate when the
  // curvature radius >> displacement, i.e. high pT / small lXY). The vertex
  // sits on the gyration circle at radius rg from a center offset by
  // (dxy - rg) from the origin, so the true dxy is recovered by inverting
  // that circle geometry instead (see EMTFTools/ParticleGuns/FlatRandomPtGunProducer2
  // ::newVertexWithD0 for the corresponding forward calculation):
  //   rg = -pT/(0.003*B*q), B = 3.811 T
  //   center = (vx - rg*sin(phi), vy + rg*cos(phi))
  //   dxy = rg + q*|center|
  {
    const double B = 3.811;  // Tesla
    double rg = -genPt / (0.003 * B * genCharge);
    double cx = vtxPos.X() - rg * std::sin(genPhi);
    double cy = vtxPos.Y() + rg * std::cos(genPhi);
    muonDxy = rg + genCharge * std::sqrt(cx * cx + cy * cy);
  }
  muonRho = vtxPos.Rho();

  vertexEta = vtxPos.eta();
  vertexPhi = vtxPos.phi();

  //parentPdgId = 0; TODO
}

MatchingResult::MatchingResult(const TrackingParticle& trackingParticle) : trackingParticle(&trackingParticle) {
  pdgId = trackingParticle.pdgId();
  genPt = trackingParticle.pt();
  genEta = trackingParticle.momentum().eta();
  genPhi = trackingParticle.momentum().phi();
  genCharge = trackingParticle.charge();

  muonDxy = this->trackingParticle->dxy();
  muonRho = this->trackingParticle->parentVertex()->position().Rho();

  vertexEta = this->trackingParticle->parentVertex()->position().eta();
  vertexPhi = this->trackingParticle->parentVertex()->position().phi();
}

int CandidateSimMuonMatcher::calcGlobalPhi(int locPhi, int proc) {
  int globPhi = 0;
  //60 degree sectors = 96 in int-scale
  globPhi = (proc) * 96 * 6 / nProcessors + locPhi;
  // first processor starts at CMS phi = 15 degrees (24 in int)... Handle wrap-around with %. Add 576 to make sure the number is positive
  globPhi = (globPhi + 24 + 576) % 576;
  return globPhi;
}

CandidateSimMuonMatcher::CandidateSimMuonMatcher(
    const edm::ParameterSet& edmCfg,
    //const OMTFConfiguration* omtfConfig,
    int nProcessors,
    const edm::ESGetToken<MagneticField, IdealMagneticFieldRecord>& magneticFieldEsToken,
    const edm::ESGetToken<Propagator, TrackingComponentsRecord>& propagatorEsToken)
    :  //omtfConfig(omtfConfig),
      nProcessors(nProcessors),
      edmCfg(edmCfg),
      magneticFieldEsToken(magneticFieldEsToken),
      propagatorEsToken(propagatorEsToken) {
  //needed when CandidateSimMuonMatcher is used outside the OMTF package, where the omtfConfig is not available
  if (edmCfg.exists("phase")) {
    if (edmCfg.getParameter<int>("phase") == 2)
      this->nProcessors = 3;
  }

  std::string muonMatcherFileName = edmCfg.getParameter<edm::FileInPath>("muonMatcherFile").fullPath();
  TFile muonMatcherFile(muonMatcherFileName.c_str());
  edm::LogImportant("l1tOmtfEventPrint") << " CandidateSimMuonMatcher: using muonMatcherFileName "
                                         << muonMatcherFileName << std::endl;

  if (edmCfg.exists("candidateSimMuonMatcherType")) {
    if (edmCfg.getParameter<std::string>("candidateSimMuonMatcherType") == "withPropagator")
      matchingType = MatchingType::withPropagator;
    else if (edmCfg.getParameter<std::string>("candidateSimMuonMatcherType") == "simplePropagation")
      matchingType = MatchingType::simplePropagation;
    else if (edmCfg.getParameter<std::string>("candidateSimMuonMatcherType") == "simpleMatching")
      matchingType = MatchingType::simpleMatching;
    else if (edmCfg.getParameter<std::string>("candidateSimMuonMatcherType") == "collectMuonCands")
      matchingType = MatchingType::collectMuonCands;

    edm::LogImportant("l1tOmtfEventPrint")
        << " CandidateSimMuonMatcher: candidateSimMuonMatcherType "
        << edmCfg.getParameter<std::string>("candidateSimMuonMatcherType") << std::endl;
  }

  edm::LogImportant("l1tOmtfEventPrint") << " CandidateSimMuonMatcher: matchingType "
                                         << static_cast<short>(matchingType) << std::endl;

  minDelta_pos = (TH1F*)muonMatcherFile.Get("minDelta_pos");
  maxDelta_pos = (TH1F*)muonMatcherFile.Get("maxDelta_pos");
  medianDelta_pos = (TH1F*)muonMatcherFile.Get("medianDelta_pos");

  minDelta_neg = (TH1F*)muonMatcherFile.Get("minDelta_neg");
  maxDelta_neg = (TH1F*)muonMatcherFile.Get("maxDelta_neg");
  medianDelta_neg = (TH1F*)muonMatcherFile.Get("medianDelta_neg");

  // Detach from file only if the histogram was actually present (null-safe)
  if (minDelta_pos)    minDelta_pos->SetDirectory(nullptr);
  if (maxDelta_pos)    maxDelta_pos->SetDirectory(nullptr);
  if (medianDelta_pos) medianDelta_pos->SetDirectory(nullptr);
  if (minDelta_neg)    minDelta_neg->SetDirectory(nullptr);
  if (maxDelta_neg)    maxDelta_neg->SetDirectory(nullptr);
  if (medianDelta_neg) medianDelta_neg->SetDirectory(nullptr);
}

CandidateSimMuonMatcher::~CandidateSimMuonMatcher() {
  delete minDelta_pos;
  delete maxDelta_pos;
  delete medianDelta_pos;
  delete minDelta_neg;
  delete maxDelta_neg;
  delete medianDelta_neg;
}

void CandidateSimMuonMatcher::beginRun(const edm::EventSetup& eventSetup) {
  //TODO use edm::ESWatcher<MagneticField> magneticFieldRecordWatcher;
  magField = eventSetup.getHandle(magneticFieldEsToken);
  // Only load the propagator for modes that actually use it.
  // simpleMatching and collectMuonCands work purely on gen-level kinematics.
  if (matchingType == MatchingType::withPropagator || matchingType == MatchingType::simplePropagation) {
    propagator = eventSetup.getHandle(propagatorEsToken);
  }
}

void CandidateSimMuonMatcher::observeEventBegin(const edm::Event& event) { gbCandidates.clear(); }

void CandidateSimMuonMatcher::observeProcesorEmulation(unsigned int iProcessor,
                                                       l1t::tftype mtfType,
                                                       const std::shared_ptr<OMTFinput>& input,
                                                       const AlgoMuons& algoCandidates,
                                                       const AlgoMuons& gbCandidates,
                                                       const FinalMuons& finalMuons) {
  //debug
  //unsigned int procIndx = omtfConfig->getProcIndx(iProcessor, mtfType);
  for (auto& gbCandidate : gbCandidates) {
    if (gbCandidate->getPtConstr() > 0) {
      LogTrace("l1tOmtfEventPrint") << "CandidateSimMuonMatcher::observeProcesorEmulation iProcessor " << iProcessor
                                    << " mtfType " << mtfType << *gbCandidate << std::endl;
      this->gbCandidates.emplace_back(gbCandidate);
    }
  }
}

bool simTrackIsMuonInOmtf(const SimTrack& simTrack) {
  if (std::abs(simTrack.type()) == 13 || std::abs(simTrack.type()) == 1000015) {  // 1000015 is stau
    //only muons
  } else
    return false;

  //in the overlap, the propagation of muons with pt less then ~3.2 fails - the actual threshold depends slightly on eta,
  if (simTrack.momentum().pt() < 2.5)
    return false;

  LogTrace("l1tOmtfEventPrint") << "simTrackIsMuonInOmtf: simTrack type " << std::setw(3) << simTrack.type() << " pt "
                                << std::setw(9) << simTrack.momentum().pt() << " eta " << std::setw(9)
                                << simTrack.momentum().eta() << " phi " << std::setw(9) << simTrack.momentum().phi()
                                << std::endl;

  //some margin for matching must be used on top of actual OMTF region,
  //i.e. (0.82-1.24)=>(0.72-1.3),
  //otherwise many candidates are marked as ghosts
  if ((std::abs(simTrack.momentum().eta()) >= 0.7) && (std::abs(simTrack.momentum().eta()) <= 1.31)) {
    LogTrace("l1tOmtfEventPrint") << "simTrackIsMuonInOmtf: is in OMTF";
  } else {
    LogTrace("l1tOmtfEventPrint") << "simTrackIsMuonInOmtf: not in OMTF";
    return false;
  }

  return true;
}

bool simTrackIsMuonInOmtfBx0(const SimTrack& simTrack) {
  if (simTrack.eventId().bunchCrossing() != 0)
    return false;

  return simTrackIsMuonInOmtf(simTrack);
}

bool simTrackIsMuonInBx0(const SimTrack& simTrack) {
  if (std::abs(simTrack.type()) == 13 || std::abs(simTrack.type()) == 1000015) {  // 1000015 is stau
    if (simTrack.momentum().pt() < 2.5)  //TODO muons with pt < 2.5 GeV do not reach the OMTF
      return false;

    //only muons
    if (simTrack.eventId().bunchCrossing() == 0)
      return true;
  }
  return false;
}

bool trackingParticleIsMuonInBx0(const TrackingParticle& trackingParticle) {
  if (std::abs(trackingParticle.pdgId()) == 13 ||
      std::abs(trackingParticle.pdgId()) ==
          1000015) {  // 1000015 is stau, todo use other selection (e.g. pt>20) if needed

    //in the overlap, the propagation of muons with pt less then ~3.2 fails - the actual threshold depends slightly on eta,
    if (trackingParticle.pt() < 2.5)
      return false;

    //only muons
    if (trackingParticle.eventId().bunchCrossing() == 0)
      return true;
  }
  return false;
}

bool trackingParticleIsMuonInBx0Displ(const TrackingParticle& trackingParticle) {
  if (std::abs(trackingParticle.pdgId()) == 13 ||
      std::abs(trackingParticle.pdgId()) ==
          1000015) {  // 1000015 is stau, todo use other selection (e.g. pt>20) if needed

    //in the overlap, the propagation of muons with pt less then ~3.2 fails - the actual threshold depends slightly on eta,
    if (trackingParticle.pt() < 2.)
      return false;

    if (trackingParticle.dxy() < 30) {
      if ((std::abs(trackingParticle.momentum().eta()) < 0.7) || (std::abs(trackingParticle.momentum().eta()) > 1.31))
        return false;
    } else {
      if ((std::abs(trackingParticle.parentVertex()->position().eta()) < 0.7) ||
          (std::abs(trackingParticle.parentVertex()->position().eta()) > 1.4))
        return false;
    }

    //only muons
    if (trackingParticle.eventId().bunchCrossing() == 0)
      return true;
  }
  return false;
}

bool trackingParticleIsMuonInOmtfBx0(const TrackingParticle& trackingParticle) {
  if (trackingParticle.eventId().bunchCrossing() != 0)
    return false;

  if (std::abs(trackingParticle.pdgId()) == 13 || std::abs(trackingParticle.pdgId()) == 1000015) {
    // 1000015 is stau, todo use other selection (e.g. pt>20) if needed
    //only muons
  } else
    return false;

  //in the overlap, the propagation of muons with pt less then ~3.2 fails, the actual threshold depends slightly on eta,
  if (trackingParticle.pt() < 2.5)
    return false;

  if (trackingParticle.parentVertex().isNonnull())
    LogTrace("l1tOmtfEventPrint") << "trackingParticleIsMuonInOmtfBx0, pdgId " << std::setw(3)
                                  << trackingParticle.pdgId() << " pt " << std::setw(9) << trackingParticle.pt()
                                  << " eta " << std::setw(9) << trackingParticle.momentum().eta() << " phi "
                                  << std::setw(9) << trackingParticle.momentum().phi() << " event "
                                  << trackingParticle.eventId().event() << " trackId "
                                  << trackingParticle.g4Tracks().at(0).trackId() << " parentVertex Rho "
                                  << trackingParticle.parentVertex()->position().Rho() << " eta "
                                  << trackingParticle.parentVertex()->position().eta() << " phi "
                                  << trackingParticle.parentVertex()->position().phi() << std::endl;
  else
    LogTrace("l1tOmtfEventPrint") << "trackingParticleIsMuonInOmtfBx0, pdgId " << std::setw(3)
                                  << trackingParticle.pdgId() << " pt " << std::setw(9) << trackingParticle.pt()
                                  << " eta " << std::setw(9) << trackingParticle.momentum().eta() << " phi "
                                  << std::setw(9) << trackingParticle.momentum().phi() << " trackId "
                                  << trackingParticle.g4Tracks().at(0).trackId();

  //some margin for matching must be used on top of actual OMTF region,
  //i.e. (0.82-1.24)=>(0.72-1.3),
  //otherwise many candidates are marked as ghosts
  if ((std::abs(trackingParticle.momentum().eta()) >= 0.7) && (std::abs(trackingParticle.momentum().eta()) <= 1.31)) {
  } else
    return false;

  return true;
}

bool trackingParticleIsMuonInOmtfEvent0(const TrackingParticle& trackingParticle) {
  if (trackingParticle.eventId().event() != 0)
    return false;

  return trackingParticleIsMuonInOmtfBx0(trackingParticle);
}

void CandidateSimMuonMatcher::observeEventEnd(const edm::Event& event,
                                              std::unique_ptr<l1t::RegionalMuonCandBxCollection>& finalCandidates) {
  LogTrace("l1tOmtfEventPrint") << "\nCandidateSimMuonMatcher::observeEventEnd" << std::endl;
  AlgoMuons ghostBustedProcMuons;
  std::vector<const l1t::RegionalMuonCand*> ghostBustedRegionalCands =
      CandidateSimMuonMatcher::ghostBust(finalCandidates.get(), gbCandidates, ghostBustedProcMuons);

  matchingResults.clear();
  if (edmCfg.exists("simTracksTag")) {
    edm::Handle<edm::SimTrackContainer> simTraksHandle;
    event.getByLabel(edmCfg.getParameter<edm::InputTag>("simTracksTag"), simTraksHandle);

    edm::Handle<edm::SimVertexContainer> simVertices;
    event.getByLabel(edmCfg.getParameter<edm::InputTag>("simVertexesTag"), simVertices);

    LogTrace("l1tOmtfEventPrint") << "simTraksHandle size " << simTraksHandle.product()->size() << std::endl;
    LogTrace("l1tOmtfEventPrint") << "simVertices size " << simVertices.product()->size() << std::endl;

    if (matchingType == MatchingType::simpleMatching) {
      std::function<bool(const SimTrack&)> const& simTrackFilter = simTrackIsMuonInOmtfBx0;
      //simTrackIsMuonInOmtfBx0 provides appropriate eta cut

      matchingResults = matchSimple(ghostBustedRegionalCands,
                                    ghostBustedProcMuons,
                                    simTraksHandle.product(),
                                    simVertices.product(),
                                    simTrackFilter);
    } else if (matchingType == MatchingType::withPropagator || matchingType == MatchingType::simplePropagation) {
      //TODO  use other simTrackFilter if needed  <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
      //withPropagator, we dont want to check the eta of the generated muon, as it is on the vertex,
      //instead inside match, we check the eta of the propagated track to the second muons station
      std::function<bool(const SimTrack&)> const& simTrackFilter =
          (matchingType == MatchingType::withPropagator) ? simTrackIsMuonInBx0 : simTrackIsMuonInOmtfBx0;
      //withPropagator : simplePropagation

      matchingResults = match(ghostBustedRegionalCands,
                              ghostBustedProcMuons,
                              simTraksHandle.product(),
                              simVertices.product(),
                              simTrackFilter);
    }
    //todo something when noMatcher

  } else if (edmCfg.exists("trackingParticleTag")) {
    edm::Handle<TrackingParticleCollection> trackingParticleHandle;
    event.getByLabel(edmCfg.getParameter<edm::InputTag>("trackingParticleTag"), trackingParticleHandle);
    LogTrace("l1tOmtfEventPrint") << "\nCandidateSimMuonMatcher::observeEventEnd trackingParticleHandle size "
                                  << trackingParticleHandle.product()->size() << std::endl;

    //TODO use other trackParticleFilter if needed <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    //TrackingParticle is used for minBias sample. There we should do propagation to match pions and kaons
    std::function<bool(const TrackingParticle&)> trackParticleFilter = trackingParticleIsMuonInBx0Displ;
    //trackingParticleIsMuonInBx0; if propagation is used, use trackingParticleIsMuonInOmtfBx0
    matchingResults =
        match(ghostBustedRegionalCands, ghostBustedProcMuons, trackingParticleHandle.product(), trackParticleFilter);
  } else {
    //No gen-truth source configured at all (neither simTracksTag nor trackingParticleTag), e.g. pure
    //background/noise samples such as MinBias/QCD/neutrino-gun productions. Rather than silently leaving
    //matchingResults empty for the whole event (which makes OMTFHitsTree empty exactly where fake/ghost
    //studies are needed), always fall back to collecting ALL ghost-busted OMTF candidates unmatched.
    //The gen-muon match fields in DataROOTDumper2 are optional and default to their "no match" sentinel
    //values (muonEvent=-1, muonPt=0, hasGenMatch=false) in this case.
    //This subsumes the previous explicit "candidateSimMuonMatcherType=collectMuonCands" opt-in, which is
    //still honored above (matchingType is set from that parameter), but is no longer required to get this
    //behaviour: it is now the natural fallback whenever no truth collection is available.
    matchingResults = collectMuonCands(ghostBustedRegionalCands, ghostBustedProcMuons);
  }
}

void CandidateSimMuonMatcher::endJob() {}

std::vector<const l1t::RegionalMuonCand*> CandidateSimMuonMatcher::ghostBust(
    const l1t::RegionalMuonCandBxCollection* mtfCands, const AlgoMuons& gbCandidates, AlgoMuons& ghostBustedProcMuons) {
  if (gbCandidates.size() != mtfCands->size(0)) {
    edm::LogError("l1tOmtfEventPrint") << "CandidateSimMuonMatcher::ghostBust(): gbCandidates.size() "
                                       << gbCandidates.size() << " != mtfCands.size() " << mtfCands->size();
  }

  boost::dynamic_bitset<> isKilled(mtfCands->size(0), false);

  for (unsigned int i1 = 0; i1 < mtfCands->size(0); ++i1) {
    if (mtfCands->at(0, i1).hwPt() == 0)
      continue;
    LogTrace("l1tOmtfEventPrint") << "\nCandidateSimMuonMatcher::ghostBust regionalCand pt " << std::setw(3)
                                  << mtfCands->at(0, i1).hwPt() << " qual " << std::setw(2)
                                  << mtfCands->at(0, i1).hwQual() << " proc " << std::setw(2)
                                  << mtfCands->at(0, i1).processor();
    for (unsigned int i2 = i1 + 1; i2 < mtfCands->size(0); ++i2) {
      auto& mtfCand1 = mtfCands->at(0, i1);
      auto& mtfCand2 = mtfCands->at(0, i2);
      if (mtfCand2.hwPt() == 0)
        continue;

      if (std::abs(mtfCand1.hwEta() - mtfCand2.hwEta()) < (0.3 / 0.010875)) {
        int gloablHwPhi1 = calcGlobalPhi(mtfCand1.hwPhi(), mtfCand1.processor());
        int gloablHwPhi2 = calcGlobalPhi(mtfCand2.hwPhi(), mtfCand2.processor());

        //folding phi
        int deltaPhi = std::abs(gloablHwPhi1 - gloablHwPhi2);
        if (deltaPhi > 576 / 2)
          deltaPhi = std::abs(deltaPhi - 576);

        //one can use the phi in radians like that:
        //double globalPhi1 = hwGmtPhiToGlobalPhi(omtfConfig->calcGlobalPhi( mtfCand1.hwPhi(), mtfCand1.processor() ) );
        //double globalPhi2 = hwGmtPhiToGlobalPhi(omtfConfig->calcGlobalPhi( mtfCand2.hwPhi(), mtfCand2.processor() ) );

        //0.0872664626 = 5 deg, i.e. the same window as in the OMTF ghost buster
        if (deltaPhi < 10) {
          //if (mtfCand1.hwQual() > mtfCand2.hwQual()) { //TODO this is used in the uGMT, and in the L1MuonAnalyzer/src/MuonMatcher.cc
          //but this should be better, the difference is samll. And it expalins the difference between the results from L1MuonAnalyzer
          if (gbCandidates[i1]->getFiredLayerCnt() > gbCandidates[i2]->getFiredLayerCnt()) {
            isKilled[i2] = true;
          } else
            isKilled[i1] = true;
        }
      }
    }
  }

  std::vector<const l1t::RegionalMuonCand*> resultCands;

  for (unsigned int i1 = 0; i1 < mtfCands->size(0); ++i1) {
    //dropping candidates with quality 0 and 1 !!!!!!!!!!!!!!!!!!!! fixme if not needed
    if (!isKilled[i1] && mtfCands->at(0, i1).hwQual() > 1) {
      resultCands.push_back(&(mtfCands->at(0, i1)));
      ghostBustedProcMuons.push_back(gbCandidates.at(i1));
    }

    if (mtfCands->at(0, i1).hwPt()) {
      LogTrace("l1tOmtfEventPrint") << "CandidateSimMuonMatcher::ghostBust\n  regionalCand pt " << std::setw(3)
                                    << mtfCands->at(0, i1).hwPt() << " qual " << std::setw(2)
                                    << mtfCands->at(0, i1).hwQual() << " proc " << std::setw(2)
                                    << mtfCands->at(0, i1).processor() << " eta " << std::setw(4)
                                    << mtfCands->at(0, i1).hwEta() << " gloablEta " << std::setw(8)
                                    << mtfCands->at(0, i1).hwEta() * 0.010875 << " hwPhi " << std::setw(3)
                                    << mtfCands->at(0, i1).hwPhi() << " globalPhi " << std::setw(8)
                                    << hwGmtPhiToGlobalPhi(
                                           calcGlobalPhi(mtfCands->at(0, i1).hwPhi(), mtfCands->at(0, i1).processor()))
                                    << " fireadLayers " << std::bitset<18>(mtfCands->at(0, i1).trackAddress().at(0))
                                    << " gb isKilled " << isKilled.test(i1) << std::endl;

      LogTrace("l1tOmtfEventPrint") << *(gbCandidates.at(i1)) << std::endl;
    }
  }

  if (resultCands.size() >= 3)
    LogTrace("l1tOmtfEventPrint") << " ghost !!!!!! " << std::endl;
  LogTrace("l1tOmtfEventPrint") << std::endl;

  return resultCands;
}

TrajectoryStateOnSurface CandidateSimMuonMatcher::atStation1(const FreeTrajectoryState& ftsStart) const {
  // propagate to MB1, which defines the OMTF region (W+-2 MB1 is connected only to the OMTF)
  // 415 cm is R of RB1in, 660.5cm is |z| of the edge of MB2 (B field on)
  ReferenceCountingPointer<Surface> rpc = ReferenceCountingPointer<Surface>(new BoundCylinder(
      GlobalPoint(0., 0., 0.), TkRotation<float>(), SimpleCylinderBounds(431.133, 431.133, -660.5, 660.5)));
  //N.B. zMin and zMax do not matter for the propagator->propagate, i.e. there is not cut on them
  TrajectoryStateOnSurface trackAtRPC = propagator->propagate(ftsStart, *rpc);

  return trackAtRPC;
}

TrajectoryStateOnSurface CandidateSimMuonMatcher::atStation2(const FreeTrajectoryState& ftsStart) const {
  ReferenceCountingPointer<Surface> surface = ReferenceCountingPointer<Surface>(new BoundCylinder(
      GlobalPoint(0., 0., 0.), TkRotation<float>(), SimpleCylinderBounds(512.401, 512.401, -900, 900)));

  TrajectoryStateOnSurface tsof = propagator->propagate(ftsStart, *surface);
  return tsof;
}

FreeTrajectoryState CandidateSimMuonMatcher::simTrackToFts(const SimTrack& simTrackPtr, const SimVertex& simVertex) {
  int charge = simTrackPtr.charge();  //simTrackPtr.type() > 0 ? -1 : 1;  //works for muons

  GlobalVector p3GV(simTrackPtr.momentum().x(), simTrackPtr.momentum().y(), simTrackPtr.momentum().z());
  GlobalPoint r3GP(simVertex.position().x(), simVertex.position().y(), simVertex.position().z());

  GlobalTrajectoryParameters tPars(r3GP, p3GV, charge, &*magField);

  return FreeTrajectoryState(tPars);
}

FreeTrajectoryState CandidateSimMuonMatcher::simTrackToFts(const TrackingParticle& trackingParticle) {
  int charge = trackingParticle.charge();  //trackingParticle.pdgId() > 0 ? -1 : 1;  //works for muons

  GlobalVector p3GV(trackingParticle.momentum().x(), trackingParticle.momentum().y(), trackingParticle.momentum().z());
  GlobalPoint r3GP(trackingParticle.vx(), trackingParticle.vy(), trackingParticle.vz());

  GlobalTrajectoryParameters tPars(r3GP, p3GV, charge, &*magField);

  return FreeTrajectoryState(tPars);
}

TrajectoryStateOnSurface CandidateSimMuonMatcher::propagate(const SimTrack& simTrack,
                                                            const edm::SimVertexContainer* simVertices) {
  SimVertex simVertex;
  int vtxInd = simTrack.vertIndex();
  if (vtxInd < 0) {
    edm::LogImportant("l1tOmtfEventPrint") << "Track with no vertex, defaulting to (0,0,0)";
  } else {
    simVertex = simVertices->at(vtxInd);
    if (((int)simVertex.vertexId()) != vtxInd) {
      edm::LogImportant("l1tOmtfEventPrint") << "simVertex.vertexId() != vtxInd. simVertex.vertexId() "
                                             << simVertex.vertexId() << " vtxInd " << vtxInd << " !!!!!!!!!!!!!!!!!";
    }
  }

  FreeTrajectoryState ftsTrack = simTrackToFts(simTrack, simVertex);

  TrajectoryStateOnSurface tsof = atStation2(ftsTrack);  //propagation

  return tsof;
}

TrajectoryStateOnSurface CandidateSimMuonMatcher::propagate(const TrackingParticle& trackingParticle) {
  FreeTrajectoryState ftsTrack = simTrackToFts(trackingParticle);

  TrajectoryStateOnSurface tsof = atStation2(ftsTrack);  //propagation

  return tsof;
}

float normal_pdf(float x, float m, float s) {
  static const float inv_sqrt_2pi = 0.3989422804014327;
  float a = (x - m) / s;

  return inv_sqrt_2pi / s * std::exp(-0.5 * a * a);
}

void CandidateSimMuonMatcher::match(const l1t::RegionalMuonCand* muonCand,
                                    const AlgoMuonPtr& procMuon,
                                    MatchingResult& result) {
  double trackPt = result.genPt;

  double candGloablEta = muonCand->hwEta() * 0.010875;
  //if (std::abs(simTrack.momentum().eta() - candGloablEta) < 0.3) //has no sense for displaced muons
  {
    double candGlobalPhi = calcGlobalPhi(muonCand->hwPhi(), muonCand->processor());
    candGlobalPhi = hwGmtPhiToGlobalPhi(candGlobalPhi);

    result.muonCand = muonCand;
    result.procMuon = procMuon;

    if (matchingType == MatchingType::simplePropagation) {
      //int charge = result.pdgId > 0 ? -1 : 1;  //works for muons

      TH1* medianDelta = nullptr;
      TH1* minDelta = nullptr;
      TH1* maxDelta = nullptr;

      if (result.genCharge == 1) {
        medianDelta = medianDelta_pos;
        minDelta = minDelta_pos;
        maxDelta = maxDelta_pos;
      } else {
        medianDelta = medianDelta_neg;
        minDelta = minDelta_neg;
        maxDelta = maxDelta_neg;
      }

      auto ptBin = medianDelta->FindBin(trackPt);
      auto min = minDelta->GetBinContent(ptBin);
      auto max = maxDelta->GetBinContent(ptBin);

      result.propagatedPhi = foldPhi(result.genPhi + medianDelta->GetBinContent(ptBin));

      result.deltaPhi = foldPhi(result.genPhi - candGlobalPhi);
      result.deltaEta = result.propagatedEta - candGloablEta;

      result.matchingLikelihood = 1. / (std::abs(result.deltaPhi) + 0.001);

      if (result.deltaPhi > min && result.deltaPhi < max && std::abs(result.deltaEta) < 0.4)
        result.result = MatchingResult::ResultType::matched;
    } else if (matchingType == MatchingType::withPropagator) {
      result.deltaPhi = foldPhi(result.propagatedPhi - candGlobalPhi);
      result.deltaEta = result.propagatedEta - candGloablEta;  //propagatedEta is set in propagate

      result.matchingLikelihood = 1. / (std::abs(result.deltaPhi) + 0.001);

      double mean = 0;
      double treshold = 0;

      //for displaced muons in H2ll
      //here a coarse threshold are used, as for dispalced muons
      treshold = 0.15;   //pt > 30
      if (trackPt < 10)  //TODO!!!!!!!!!!!!!!!!!!!!! tune the threshold!!!!!!
        treshold = 0.4;
      else if (trackPt < 30)  //TODO!!!!!!!!!!!!!!! tune the threshold!!!!!!
        treshold = 0.22;

      mean = 0;

      if (std::abs(result.deltaPhi - mean) < treshold && std::abs(result.deltaEta) < 0.4)
        result.result = MatchingResult::ResultType::matched;
    }

    LogTrace("l1tOmtfEventPrint") << "CandidateSimMuonMatcher::match: simTrack type " << result.pdgId << " pt "
                                  << std::setw(8) << trackPt << " eta " << std::setw(8) << result.genEta << " phi "
                                  << std::setw(8) << result.genPhi << " propagatedEta " << std::setw(8)
                                  << result.propagatedEta << " propagatedPhi " << result.propagatedPhi
                                  << "\n             muonCand pt " << std::setw(8) << muonCand->hwPt()
                                  << " candGloablEta " << std::setw(8) << candGloablEta << " candGlobalPhi "
                                  << std::setw(8) << candGlobalPhi << " hwPhi " << muonCand->hwPhi() << " hwQual "
                                  << muonCand->hwQual() << " processor " << muonCand->processor() << " deltaEta "
                                  << std::setw(8) << result.deltaEta << " deltaPhi " << std::setw(8) << result.deltaPhi
                                  << " Likelihood " << std::setw(8) << result.matchingLikelihood << " result "
                                  << (short)result.result << std::endl;
  }
}

std::vector<MatchingResult> CandidateSimMuonMatcher::cleanMatching(std::vector<MatchingResult> matchingResults,
                                                                   std::vector<const l1t::RegionalMuonCand*>& muonCands,
                                                                   AlgoMuons& ghostBustedProcMuons) {
  //Cleaning the matching
  std::sort(
      matchingResults.begin(), matchingResults.end(), [](const MatchingResult& a, const MatchingResult& b) -> bool {
        return a.matchingLikelihood > b.matchingLikelihood;
      });

  for (unsigned int i1 = 0; i1 < matchingResults.size(); i1++) {
    if (matchingResults[i1].result == MatchingResult::ResultType::matched) {
      for (unsigned int i2 = i1 + 1; i2 < matchingResults.size(); i2++) {
        if ((matchingResults[i1].trackingParticle &&
             matchingResults[i1].trackingParticle == matchingResults[i2].trackingParticle) ||
            (matchingResults[i1].simTrack && matchingResults[i1].simTrack == matchingResults[i2].simTrack) ||
            (matchingResults[i1].muonCand == matchingResults[i2].muonCand)) {
          //if matchingResults[i1].muonCand == false, then it is also OK here
          matchingResults[i2].result = MatchingResult::ResultType::duplicate;
        }
      }
    }
  }

  std::vector<MatchingResult> cleanedMatchingResults;
  for (auto& matchingResult : matchingResults) {
    if (matchingResult.result == MatchingResult::ResultType::matched || matchingResult.muonCand == nullptr)
      //adding also the simTracks that are not matched at all, before it is assured that they are not duplicates
      cleanedMatchingResults.push_back(matchingResult);
  }

  //adding the muonCand-s that were not matched, e.g. in order to analyze them later
  unsigned int iCand = 0;
  for (auto& muonCand : muonCands) {
    bool isMatched = false;
    for (auto& matchingResult : cleanedMatchingResults) {
      if (matchingResult.muonCand == muonCand) {
        isMatched = true;
        break;
      }
    }

    if (!isMatched) {
      MatchingResult result;
      result.muonCand = muonCand;
      result.procMuon = ghostBustedProcMuons.at(iCand);
      cleanedMatchingResults.push_back(result);
    }
    iCand++;
  }

  LogTrace("l1tOmtfEventPrint") << "\nCandidateSimMuonMatcher::cleanMatching:" << __LINE__
                                << " cleanedMatchingResults:" << std::endl;
  for (auto& result : cleanedMatchingResults) {
    if (result.trackingParticle || result.simTrack)
      LogTrace("l1tOmtfEventPrint") << "CandidateSimMuonMatcher::cleanMatching:" << __LINE__ << " simTrack type "
                                    << result.pdgId << " pt " << std::setw(8) << result.genPt << " eta " << std::setw(8)
                                    << result.genEta << " phi " << std::setw(8) << result.genPhi;
    else
      LogTrace("l1tOmtfEventPrint") << "no matched track ";

    if (result.muonCand) {
      LogTrace("l1tOmtfEventPrint") << " muonCand pt " << std::setw(8) << result.muonCand->hwPt() << " hwQual "
                                    << result.muonCand->hwQual() << " hwEta " << result.muonCand->hwEta()
                                    << " deltaEta " << std::setw(8) << result.deltaEta << " deltaPhi " << std::setw(8)
                                    << result.deltaPhi << " Likelihood " << std::setw(8) << result.matchingLikelihood
                                    << " result " << (short)result.result;
      //LogTrace("l1tOmtfEventPrint") << " procMuon " << *(result.procMuon) << std::endl;
    } else
      LogTrace("l1tOmtfEventPrint") << " no muonCand "
                                    << " result " << (short)result.result << std::endl;
  }
  LogTrace("l1tOmtfEventPrint") << " " << std::endl;

  return cleanedMatchingResults;
}

void CandidateSimMuonMatcher::propagate(MatchingResult& result) {
  auto doSimplePropagation = [&]() {
    TH1* medianDelta = result.genCharge == 1 ? medianDelta_pos : medianDelta_neg;
    auto ptBin = medianDelta->FindBin(result.genPt);
    result.propagatedPhi = foldPhi(result.genPhi + medianDelta->GetBinContent(ptBin));
    result.propagatedEta = result.genEta;
  };

  if (matchingType == MatchingType::withPropagator) {
    //TODO if result.simVertex is null it will crash - add a protection
    FreeTrajectoryState ftsTrack = result.simTrack ? simTrackToFts(*(result.simTrack), *(result.simVertex))
                                                   : simTrackToFts(*result.trackingParticle);

    TrajectoryStateOnSurface tsof = atStation2(ftsTrack);  //propagation

    if (!tsof.isValid()) {
      if (result.genPt > 2.5 && result.trackingParticle && result.trackingParticle->parentVertex().isNonnull()) {
        if (result.muonRho < 40) {
          doSimplePropagation();
        }
      } else {
        result.result = MatchingResult::ResultType::propagationFailed;
        //result.propagatedEta = 0 in this case
      }
    } else {
      result.propagatedPhi = tsof.globalPosition().phi();
      result.propagatedEta = tsof.globalPosition().eta();
    }
  } else if (matchingType == MatchingType::simplePropagation) {
    doSimplePropagation();
  }
}

void CandidateSimMuonMatcher::match(std::vector<const l1t::RegionalMuonCand*>& muonCands,
                                    AlgoMuons& ghostBustedProcMuons,
                                    MatchingResult& result,
                                    std::vector<MatchingResult>& matchingResults) {
  propagate(result);
  if (result.result == MatchingResult::ResultType::propagationFailed) {  //no sense to do matching

    //TODO For the displaced muons adding the muons for which the propagation failed in principle has no sense
    //as matching with candidates is not possible then.
    //For prompt muons this can be useful, to have the full pt spectrum of gen muons.
    //However, using them in the denominator of the efficiency biases the efficiency, because for these muons matching to the candidates is not possible.
    //In any case these results are marked in the DataROOTDumper2 omtfEvent.muonEvent = -2.
    LogTrace("l1tOmtfEventPrint") << __FUNCTION__ << ":" << __LINE__ << " propagation failed: genPt " << result.genPt
                                  << " genEta " << result.genEta << " eventId "  //<< simTrack.eventId().event()
                                  << std::endl;

    //matchingResults.push_back(result);
  } else {
    //checking if the propagated track is inside the OMTF range, TODO - tune the range!!!!!!!!!!!!!!!!!
    //eta 0.7 is the beginning of the MB2,
    //the eta range wider than the nominal OMTF region is needed, as in any case muons outside this region are seen by the OMTF
    //so it is better to match them to simMuon, otherwise they look like ghosts.
    //Besides, it better to train the nn suich that is able to measure its pt, as it may affect the rate
    if ((std::abs(result.propagatedEta) >= 0.7) && (std::abs(result.propagatedEta) <= 1.31)) {
      LogTrace("l1tOmtfEventPrint")
          << "CandidateSimMuonMatcher::match simTrack IS in OMTF region, matching to the omtfCands, propagatedEta: "
          << result.propagatedEta;
    } else {
      LogTrace("l1tOmtfEventPrint") << "simTrack NOT in OMTF region ";
      return;
    }

    /* TODO fix if filling of the deltaPhiPropCandMean and deltaPhiPropCandStdDev is needed
      double ptGen = simTrack.momentum().pt();
      if(ptGen >= deltaPhiVertexProp->GetXaxis()->GetXmax())
        ptGen = deltaPhiVertexProp->GetXaxis()->GetXmax() - 0.01;

      deltaPhiVertexProp->Fill(ptGen, simTrack.momentum().phi() - tsof.globalPosition().phi());*/

    unsigned int iCand = 0;
    bool matched = false;
    for (auto& muonCand : muonCands) {
      //dropping very low quality candidates, as they are fakes usually - but it has no sense, then the results are not conclusive
      if (muonCand->hwQual() > 1) {
        MatchingResult resultCopy = result;

        match(muonCand, ghostBustedProcMuons.at(iCand), resultCopy);

        if (resultCopy.result == MatchingResult::ResultType::matched) {
          matchingResults.push_back(resultCopy);
          matched = true;
        }
      }
      iCand++;
    }

    if (!matched) {  //we are adding also if it was not matching to any candidate
      matchingResults.push_back(result);
      LogTrace("l1tOmtfEventPrint") << __FUNCTION__ << ":" << __LINE__ << " no matching candidate found" << std::endl;
    }
  }
}

std::vector<MatchingResult> CandidateSimMuonMatcher::match(std::vector<const l1t::RegionalMuonCand*>& muonCands,
                                                           AlgoMuons& ghostBustedProcMuons,
                                                           const edm::SimTrackContainer* simTracks,
                                                           const edm::SimVertexContainer* simVertices,
                                                           std::function<bool(const SimTrack&)> const& simTrackFilter) {
  std::vector<MatchingResult> matchingResults;

  for (auto& simTrack : *simTracks) {
    if (!simTrackFilter(simTrack))
      continue;

    LogTrace("l1tOmtfEventPrint") << "\nCandidateSimMuonMatcher::match, simTrack type " << std::setw(3)
                                  << simTrack.type() << " pt " << std::setw(9) << simTrack.momentum().pt() << " eta "
                                  << std::setw(9) << simTrack.momentum().eta() << " phi " << std::setw(9)
                                  << simTrack.momentum().phi() << " rho "
                                  << (simTrack.vertIndex() >= 0 ? simVertices->at(simTrack.vertIndex()).position().Rho()
                                                                : -99)
                                  << std::endl;

    SimVertex simVertex;
    int vtxInd = simTrack.vertIndex();
    if (vtxInd < 0) {
      edm::LogImportant("l1tOmtfEventPrint") << "Track with no vertex, defaulting to (0,0,0)";
    } else {
      simVertex = simVertices->at(vtxInd);
      if (((int)simVertex.vertexId()) != vtxInd) {
        edm::LogImportant("l1tOmtfEventPrint") << "simVertex.vertexId() != vtxInd. simVertex.vertexId() "
                                               << simVertex.vertexId() << " vtxInd " << vtxInd << " !!!!!!!!!!!!!!!!!";
      }
    }

    MatchingResult result(simTrack, simTrack.vertIndex() >= 0 ? &(simVertices->at(simTrack.vertIndex())) : nullptr);

    match(muonCands, ghostBustedProcMuons, result, matchingResults);
  }

  return cleanMatching(matchingResults, muonCands, ghostBustedProcMuons);
}

std::vector<MatchingResult> CandidateSimMuonMatcher::match(
    std::vector<const l1t::RegionalMuonCand*>& muonCands,
    AlgoMuons& ghostBustedProcMuons,
    const TrackingParticleCollection* trackingParticles,
    std::function<bool(const TrackingParticle&)> const& simTrackFilter) {
  std::vector<MatchingResult> matchingResults;
  LogTrace("l1tOmtfEventPrint") << "CandidateSimMuonMatcher::match trackingParticles->size() "
                                << trackingParticles->size() << std::endl;

  for (auto& trackingParticle : *trackingParticles) {
    //LogTrace("l1tOmtfEventPrint") <<"CandidateSimMuonMatcher::match:"<<__LINE__<<" trackingParticle type "<<std::setw(3)<<trackingParticle.pdgId()<<" pt "<<std::setw(9)<<trackingParticle.pt()<<" eta "<<std::setw(9)<<trackingParticle.momentum().eta()<<" phi "<<std::setw(9)<<trackingParticle.momentum().phi()<<std::endl;

    if (simTrackFilter(trackingParticle) == false)
      continue;

    LogTrace("l1tOmtfEventPrint") << "CandidateSimMuonMatcher::match, trackingParticle type " << std::setw(3)
                                  << trackingParticle.pdgId() << " pt " << std::setw(9) << trackingParticle.pt()
                                  << " eta " << std::setw(9) << trackingParticle.momentum().eta() << " phi "
                                  << std::setw(9) << trackingParticle.momentum().phi() << std::endl;

    MatchingResult result(trackingParticle);

    match(muonCands, ghostBustedProcMuons, result, matchingResults);
  }

  return cleanMatching(matchingResults, muonCands, ghostBustedProcMuons);
}

std::vector<MatchingResult> CandidateSimMuonMatcher::matchSimple(
    std::vector<const l1t::RegionalMuonCand*>& muonCands,
    AlgoMuons& ghostBustedProcMuons,
    const edm::SimTrackContainer* simTracks,
    const edm::SimVertexContainer* simVertices,
    std::function<bool(const SimTrack&)> const& simTrackFilter) {
  std::vector<MatchingResult> matchingResults;

  for (auto& simTrack : *simTracks) {
    if (!simTrackFilter(simTrack))
      continue;

    LogTrace("l1tOmtfEventPrint") << "CandidateSimMuonMatcher::matchSimple: simTrack type " << std::setw(3)
                                  << simTrack.type() << " pt " << std::setw(9) << simTrack.momentum().pt() << " eta "
                                  << std::setw(9) << simTrack.momentum().eta() << " phi " << std::setw(9)
                                  << simTrack.momentum().phi() << std::endl;

    bool matched = false;

    unsigned int iCand = 0;
    for (auto& muonCand : muonCands) {
      //dropping very low quality candidates, as they are fakes usually - but it has no sense, then the results are not conclusive
      //if(muonCand->hwQual() > 1)
      {
        MatchingResult result(simTrack, simTrack.vertIndex() >= 0 ? &(simVertices->at(simTrack.vertIndex())) : nullptr);

        double candGloablEta = muonCand->hwEta() * 0.010875;
        double candGlobalPhi = calcGlobalPhi(muonCand->hwPhi(), muonCand->processor());
        candGlobalPhi = hwGmtPhiToGlobalPhi(candGlobalPhi);

        result.deltaPhi = foldPhi(result.genPhi - candGlobalPhi);
        result.deltaEta = result.genEta - candGloablEta;

        result.propagatedPhi = result.genPhi;
        result.propagatedEta = result.genEta;

        result.muonCand = muonCand;
        result.procMuon = ghostBustedProcMuons.at(iCand);

        //TODO histogram can be used, like in the usercode/L1MuonAnalyzer  MuonMatcher::matchWithoutPorpagation
        //for prompt muons
        /*double treshold = 0.3;
        if (simTrack.momentum().pt() < 5)
          treshold = 1.5;
        else if (simTrack.momentum().pt() < 8)
          treshold = 0.8;
        else if (simTrack.momentum().pt() < 10)
          treshold = 0.6;
        else if (simTrack.momentum().pt() < 20)
          treshold = 0.5;*/

        double treshold = 0.5;
        if (simTrack.momentum().pt() < 5)
          treshold = 1.5;
        else if (simTrack.momentum().pt() < 10)
          treshold = 1.0;
        else if (simTrack.momentum().pt() < 25)
          treshold = 0.7;

        if (std::abs(result.deltaPhi) < treshold && std::abs(result.deltaEta) < 0.5) {
          result.result = MatchingResult::ResultType::matched;
          //matchingLikelihood is needed in the cleanMatching, so we put something
          result.matchingLikelihood = 1. / (std::abs(result.deltaPhi) + 0.001);
        }

        LogTrace("l1tOmtfEventPrint") << "CandidateSimMuonMatcher::matchSimple: simTrack type " << simTrack.type()
                                      << " pt " << std::setw(8) << simTrack.momentum().pt() << " eta " << std::setw(8)
                                      << simTrack.momentum().eta() << " phi " << std::setw(8)
                                      << simTrack.momentum().phi() << "\n             muonCand pt " << std::setw(8)
                                      << muonCand->hwPt() << " candGloablEta " << std::setw(8) << candGloablEta
                                      << " candGlobalPhi " << std::setw(8) << candGlobalPhi << " hwQual "
                                      << muonCand->hwQual() << " deltaEta " << std::setw(8) << result.deltaEta
                                      << " deltaPhi " << std::setw(8) << result.deltaPhi << " matchingLikelihood "
                                      << result.matchingLikelihood << " result " << (short)result.result << std::endl;

        if (result.result == MatchingResult::ResultType::matched) {
          matchingResults.push_back(result);
          matched = true;
        }
      }
      iCand++;
    }

    if (!matched) {  //we are adding also if it was not matched to any candidate
      MatchingResult result(simTrack, simTrack.vertIndex() >= 0 ? &(simVertices->at(simTrack.vertIndex())) : nullptr);
      result.propagatedPhi = result.genPhi;
      result.propagatedEta = result.genEta;
      matchingResults.push_back(result);
      LogTrace("l1tOmtfEventPrint") << __FUNCTION__ << ":" << __LINE__ << " no matching candidate found" << std::endl;
    }
  }

  return cleanMatching(matchingResults, muonCands, ghostBustedProcMuons);
}

std::vector<MatchingResult> CandidateSimMuonMatcher::collectMuonCands(
    std::vector<const l1t::RegionalMuonCand*>& muonCands, AlgoMuons& ghostBustedProcMuons) {
  std::vector<MatchingResult> matchingResults;
  unsigned int iCand = 0;
  for (auto& muonCand : muonCands) {
    //dropping very low quality candidates, as they are fakes usually - but it has no sense, then the results are not conclusive
    //if(muonCand->hwQual() > 1)
    {
      MatchingResult result;

      result.muonCand = muonCand;
      result.procMuon = ghostBustedProcMuons.at(iCand);

      double candGloablEta = muonCand->hwEta() * 0.010875;
      double candGlobalPhi = calcGlobalPhi(muonCand->hwPhi(), muonCand->processor());
      candGlobalPhi = hwGmtPhiToGlobalPhi(candGlobalPhi);

      LogTrace("l1tOmtfEventPrint") << "CandidateSimMuonMatcher::collectMuonCands: muonCand pt " << std::setw(8)
                                    << muonCand->hwPt() << " candGloablEta " << std::setw(8) << candGloablEta
                                    << " candGlobalPhi " << std::setw(8) << candGlobalPhi << " hwQual "
                                    << muonCand->hwQual() << " deltaEta " << std::setw(8) << result.deltaEta
                                    << " deltaPhi " << std::setw(8) << result.deltaPhi << " matchingLikelihood "
                                    << result.matchingLikelihood << " result " << (short)result.result << std::endl;

      matchingResults.push_back(result);
    }
    iCand++;
  }
  return matchingResults;
}
