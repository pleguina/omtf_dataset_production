/*
 * IOMTFReconstructionObserver.h
 *
 *  Created on: Oct 12, 2017
 *      Author: kbunkow
 */

#ifndef L1T_OmtfP1_IOMTFRECONSTRUCTIONOBSERVER_H_
#define L1T_OmtfP1_IOMTFRECONSTRUCTIONOBSERVER_H_

#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/AlgoMuon.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/FinalMuon.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/OMTFinput.h"

#include "FWCore/Framework/interface/EventSetup.h"
#include "DataFormats/L1TMuon/interface/RegionalMuonCand.h"
#include "DataFormats/L1TMuon/interface/RegionalMuonCandFwd.h"

#include <boost/property_tree/ptree.hpp>

#include <memory>
#include <vector>

// Forward declarations
class RefHitDef;
class StubResult;
class Key;
class GoldenPatternResult;
class AlgoMuon;
typedef std::shared_ptr<AlgoMuon> AlgoMuonPtr;
typedef std::vector<AlgoMuonPtr> AlgoMuons;

namespace edm {
  class Event;
} /* namespace edm */

class IOMTFEmulationObserver {
public:
  IOMTFEmulationObserver();
  virtual ~IOMTFEmulationObserver();

  virtual void beginRun(edm::EventSetup const& eventSetup) {}

  virtual void observeProcesorBegin(unsigned int iProcessor, l1t::tftype mtfType) {}

  virtual void addProcesorData(std::string key, boost::property_tree::ptree& procDataTree) {}

  virtual void observeProcesorEmulation(unsigned int iProcessor,
                                        l1t::tftype mtfType,
                                        const std::shared_ptr<OMTFinput>& input,
                                        const AlgoMuons& algoCandidates,
                                        const AlgoMuons& gbCandidates,
                                        const FinalMuons& finalMuons) = 0;

  virtual void observeEventBegin(const edm::Event& iEvent) {}

  virtual void observeEventEnd(const edm::Event& iEvent,
                               std::unique_ptr<l1t::RegionalMuonCandBxCollection>& finalCandidates) {};

  virtual void observeGoldenPatternResults(unsigned int iProcessor,
                                          unsigned int iRefHit,
                                          const RefHitDef& refHitDef,
                                          unsigned int iGP,
                                          unsigned int iLayer,
                                          const StubResult& stubResult,
                                          int phiDistMin) {}

  virtual void observeGoldenPatternFinalResults(unsigned int iProcessor,
                                               unsigned int iGP,
                                               const Key& gpKey,
                                               unsigned int iRefHit,
                                               const GoldenPatternResult& gpResult) {}

  virtual void observeSortedCandidates(unsigned int iProcessor,
                                      l1t::tftype mtfType,
                                      const AlgoMuons& algoCandidates) {}

  virtual void endJob() = 0;
};

#endif /* L1T_OmtfP1_IOMTFRECONSTRUCTIONOBSERVER_H_ */
