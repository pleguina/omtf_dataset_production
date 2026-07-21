/*
 * XMLEventWriter.h
 *
 *  Created on: Oct 12, 2017
 *      Author: kbunkow
 */

#ifndef L1T_OmtfP1_XMLEVENTWRITER_H_
#define L1T_OmtfP1_XMLEVENTWRITER_H_

#include "DataFormats/L1TMuon/interface/RegionalMuonCand.h"
#include "DataFormats/L1TMuon/interface/RegionalMuonCandFwd.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/AlgoMuon.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/IOMTFEmulationObserver.h"

#include <boost/property_tree/ptree.hpp>

#include <memory>
#include <string>
#include <vector>

class XMLEventWriter : public IOMTFEmulationObserver {
public:
  // eventsPerFile > 0 enables splitting: each batch of eventsPerFile events is
  // written to a separate file named <base>_part000<ext>, <base>_part001<ext>, ...
  XMLEventWriter(const OMTFConfiguration* aOMTFConfig, std::string fName, int eventsPerFile = 0);

  ~XMLEventWriter() override;

  void observeProcesorBegin(unsigned int iProcessor, l1t::tftype mtfType) override;

  void addProcesorData(std::string key, boost::property_tree::ptree& procDataTree) override {
    procTree.add_child(key, procDataTree);
  }

  void observeProcesorEmulation(unsigned int iProcessor,
                                l1t::tftype mtfType,
                                const std::shared_ptr<OMTFinput>& input,
                                const AlgoMuons& algoCandidates,
                                const AlgoMuons& gbCandidates,
                                const FinalMuons& finalMuons) override;

  void observeEventBegin(const edm::Event& iEvent) override;

  void observeEventEnd(const edm::Event& iEvent,
                       std::unique_ptr<l1t::RegionalMuonCandBxCollection>& finalCandidates) override;

  void endJob() override;

private:
  void initTree();         // (re-)initialise tree with OMTF version attribute
  void flushCurrentTreeToFile();  // write current tree to a part file and reset

  const OMTFConfiguration* omtfConfig;

  boost::property_tree::ptree tree;

  boost::property_tree::ptree* eventTree = nullptr;

  boost::property_tree::ptree procTree;

  std::string fName;

  unsigned int eventNum = 0;

  unsigned int eventId = 0;

  int eventsPerFile = 0;       // 0 = no splitting

  unsigned int fileIndex = 0;  // current part-file index
};

#endif /* L1T_OmtfP1_XMLEVENTWRITER_H_ */
