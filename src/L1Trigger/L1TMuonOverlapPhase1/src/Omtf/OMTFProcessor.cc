/*
 * OMTFProcessor.cpp
 *
 *  Created on: Oct 7, 2017
 *      Author: kbunkow
 */
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/OMTFProcessor.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/MuonStub.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/MuonStubsInput.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/MuonStubMakerBase.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/GhostBuster.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/GhostBusterPreferRefDt.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/GoldenPatternWithStat.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/IOMTFEmulationObserver.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/OMTFinput.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/OMTFinputMaker.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/OMTFSorter.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/StubResult.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Tools/HLSDigiExporter.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/DetailedDebugExporter.h"

#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "DataFormats/MuonDetId/interface/DTChamberId.h"
#include "DataFormats/MuonDetId/interface/CSCDetId.h"
#include "DataFormats/MuonDetId/interface/RPCDetId.h"

#include <bitset>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <map>
#include <string>
#include <vector>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/timer/timer.hpp>

///////////////////////////////////////////////
///////////////////////////////////////////////
template <class GoldenPatternType>
OMTFProcessor<GoldenPatternType>::OMTFProcessor(OMTFConfiguration* omtfConfig,
                                                const edm::ParameterSet& edmCfg,
                                                edm::EventSetup const& evSetup,
                                                const L1TMuonOverlapParams* omtfPatterns)
    : ProcessorBase<GoldenPatternType>(omtfConfig, omtfPatterns) {
  init(edmCfg, evSetup);
};

template <class GoldenPatternType>
OMTFProcessor<GoldenPatternType>::OMTFProcessor(OMTFConfiguration* omtfConfig,
                                                const edm::ParameterSet& edmCfg,
                                                edm::EventSetup const& evSetup,
                                                GoldenPatternVec<GoldenPatternType>&& gps)
    : ProcessorBase<GoldenPatternType>(omtfConfig, std::forward<GoldenPatternVec<GoldenPatternType> >(gps)) {
  init(edmCfg, evSetup);
};

template <class GoldenPatternType>
OMTFProcessor<GoldenPatternType>::~OMTFProcessor() {
  if (useFloatingPointExtrapolation)
    saveExtrapolFactors();
}

template <class GoldenPatternType>
void OMTFProcessor<GoldenPatternType>::init(const edm::ParameterSet& edmCfg, edm::EventSetup const& evSetup) {
  setSorter(new OMTFSorter<GoldenPatternType>(this->myOmtfConfig->getSorterType()));
  //initialize with the default sorter

  if (this->myOmtfConfig->getGhostBusterType() == "GhostBusterPreferRefDt" ||
      this->myOmtfConfig->getGhostBusterType() == "byLLH" || this->myOmtfConfig->getGhostBusterType() == "byFPLLH" ||
      this->myOmtfConfig->getGhostBusterType() == "byRefLayer" ||
      this->myOmtfConfig->getGhostBusterType() == "byRefLayerAndHitQual") {
    setGhostBuster(new GhostBusterPreferRefDt(this->myOmtfConfig));
    edm::LogVerbatim("OMTFReconstruction") << "setting " << this->myOmtfConfig->getGhostBusterType() << std::endl;
  } else {
    setGhostBuster(new GhostBuster(this->myOmtfConfig));  //initialize with the default sorter
    edm::LogVerbatim("OMTFReconstruction") << "setting GhostBuster" << std::endl;
  }

  convertToOuputScales = [&](unsigned int iProcessor, l1t::tftype mtfType, const AlgoMuons& gbCandidates) {
    return this->convertToOuputScalesPhase1(iProcessor, mtfType, gbCandidates);
  };

  edm::LogVerbatim("OMTFReconstruction") << "fwVersion 0x" << hex << this->myOmtfConfig->fwVersion() << std::endl;

  useStubQualInExtr = this->myOmtfConfig->useStubQualInExtr();
  useEndcapStubsRInExtr = this->myOmtfConfig->useEndcapStubsRInExtr();

  if (edmCfg.exists("useFloatingPointExtrapolation"))
    useFloatingPointExtrapolation = edmCfg.getParameter<bool>("useFloatingPointExtrapolation");

  std::string extrapolFactorsFilename;
  if (edmCfg.exists("extrapolFactorsFilename")) {
    extrapolFactorsFilename = edmCfg.getParameter<edm::FileInPath>("extrapolFactorsFilename").fullPath();
  }

  if (this->myOmtfConfig->usePhiBExtrapolationMB1() || this->myOmtfConfig->usePhiBExtrapolationMB2()) {
    extrapolFactors.resize(2 * 3, std::vector<std::map<int, double> >(this->myOmtfConfig->nLayers()));
    extrapolFactorsNorm.resize(2 * 3, std::vector<std::map<int, int> >(this->myOmtfConfig->nLayers()));

    //when useFloatingPointExtrapolation is true the extrapolFactors are not used,
    //all calculations are done in the extrapolateDtPhiBFloatPoint
    if (!extrapolFactorsFilename.empty() && !useFloatingPointExtrapolation)
      loadExtrapolFactors(extrapolFactorsFilename);
  }

  edm::LogVerbatim("OMTFReconstruction") << "useFloatingPointExtrapolation " << useFloatingPointExtrapolation
                                         << std::endl;
}

template <class GoldenPatternType>
FinalMuons OMTFProcessor<GoldenPatternType>::convertToOuputScalesPhase1(unsigned int iProcessor,
                                                                        l1t::tftype mtfType,
                                                                        const AlgoMuons& gbCandidates) {
  LogTrace("l1tOmtfEventPrint") << __FUNCTION__ << ":" << __LINE__ << " gbCandidates.size() " << gbCandidates.size()
                                << std::endl;
  FinalMuons finalMuons;

  for (auto& myCand : gbCandidates) {
    FinalMuon finalMuon(myCand);

    //the charge is only for the constrained measurement. The constrained measurement is always defined for a valid candidate
    if (myCand->getPdfSumConstr() > 0 && myCand->getFiredLayerCntConstr() >= 3)
      finalMuon.setPt(myCand->getPtConstr());
    else if (myCand->getPtUnconstr() > 0)
      //if myCand->getPdfSumConstr() == 0, the myCand->getPtConstr() might not be 0, see the end of GhostBusterPreferRefDt::select
      //but 0 means empty candidate, 1 means pt=0, therefore here we set HwPt to 1, as the PtUnconstr > 0
      finalMuon.setPt(1);
    else
      finalMuon.setPt(0);

    if (finalMuon.getPt() == 0)
      continue;

    finalMuon.setSign(myCand->getChargeConstr() < 0 ? 1 : 0);

    if (mtfType == l1t::omtf_pos)
      finalMuon.setEta(myCand->getEtaHw());
    else
      finalMuon.setEta((-1) * myCand->getEtaHw());

    int phiValue = myCand->getPhi();
    if (phiValue >= int(this->myOmtfConfig->nPhiBins()))
      phiValue -= this->myOmtfConfig->nPhiBins();
    phiValue = this->myOmtfConfig->procPhiToGmtPhi(phiValue);
    finalMuon.setPhi(phiValue);

    //finalMuon.setHwSignValid(1);

    if (myCand->getPtUnconstr() >= 0) {  //emtpy getPtUnconstr is 0, so this if rather has no sense, TODO - remove it
      //the upt has different hardware scale than the pt, the upt unit is 1 GeV
      finalMuon.setPtUnconstr(myCand->getPtUnconstr());
    } else
      finalMuon.setPtUnconstr(0);

    unsigned int quality = 12;
    if (this->myOmtfConfig->fwVersion() <= 6)
      quality = checkHitPatternValidity(myCand->getFiredLayerBits()) ? 0 | (1 << 2) | (1 << 3) : 0 | (1 << 2);  //12 : 4

    if (abs(myCand->getEtaHw()) == 115 &&  //115 is eta 1.25                        rrrrrrrrccccdddddd
        (static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("100000001110000000").to_ulong() ||
         static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000001110000000").to_ulong() ||
         static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("100000000110000000").to_ulong() ||
         static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("100000001100000000").to_ulong() ||
         static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("100000001010000000").to_ulong())) {
      if (this->myOmtfConfig->fwVersion() <= 6)
        quality = 4;
      else
        quality = 1;
    }

    if (this->myOmtfConfig->fwVersion() >= 5 && this->myOmtfConfig->fwVersion() <= 6) {
      if (static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000010000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000100000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001000000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000010000000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000100000000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001000000000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("010000000000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("100000000000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000010000001100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000100000001100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001000000001100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000010000000001100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000100000000001100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001000000000001100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("010000000000001100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("100000000000001100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000010000110000").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000100000110000").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001000000110000").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000010000000110000").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000100000000110000").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001000000000110000").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("010000000000110000").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("100000000000110000").to_ulong())
        quality = 1;
    } else if (this->myOmtfConfig->fwVersion() >= 8) {  //TODO fix the fwVersion     rrrrrrrrccccdddddd
      if (static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000110000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000100000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000010000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000110000000001").to_ulong() ||

          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001000000001100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000011000000001100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000010000000001100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000011000000000100").to_ulong() ||

          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000011000000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001000010000000001").to_ulong())
        quality = 1;
      else if (
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000010000000101").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000010001000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000011000000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000011000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000011100000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000100000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000100001000100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000100100000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000110100000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000111000000000").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000111000000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000111000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001000001000100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001010000000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001010000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001010000000100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001100000000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001100000000100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001100000000111").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001100001000000").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001110000000100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001110000000101").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000010000000000101").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000010010000000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000010010000000100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000010010000000101").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000010100000000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000010100000000101").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000011110000000100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000011110000000101").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000101000000010101").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001000010000000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001000011000000000").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001000011000000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001000100000000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001000110000000000").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001001000000000100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001001100000000100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001010000000000100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("010000000010000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("010000000011000100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("010000010000000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("010000100000000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("100000011000000000").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000110000000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000010000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000110000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000011000000001100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000011000000000100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000010010000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000010000000001100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001001000001000100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001100000000101").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000100000000101").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001100000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001110000000111").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001000110001000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001110000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001000000001000100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000110001000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001000000000101").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001010000001000000").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001100000001000000").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("100000010000000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("010000010010000000").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000010100000001100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001000110000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001000000001100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000000000000111101").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000001100000110001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000100000000010100").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001000100000000011").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("001000110000000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("010000100010000001").to_ulong() ||
          static_cast<unsigned int>(myCand->getFiredLayerBits()) == std::bitset<18>("000100000000110000").to_ulong())
        quality = 8;
    }  //  if (abs(myCand->getEta()) == 121) quality = 4;
    if (abs(myCand->getEtaHw()) >= 121)
      quality = 0;  // changed from 4 on request from HI

    finalMuon.setQuality(quality);
    finalMuons.push_back(finalMuon);
  }
  return finalMuons;
}
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
template <class GoldenPatternType>
std::vector<l1t::RegionalMuonCand> OMTFProcessor<GoldenPatternType>::getRegionalMuonCands(unsigned int iProcessor,
                                                                                          l1t::tftype mtfType,
                                                                                          FinalMuons& finalMuons) {
  std::vector<l1t::RegionalMuonCand> result;

  for (auto& finalMuon : finalMuons) {
    l1t::RegionalMuonCand candidate;

    candidate.setHwPt(finalMuon.getPt());
    candidate.setHwPtUnconstrained(finalMuon.getPtUnconstr());

    candidate.setHwPhi(finalMuon.getPhi());
    candidate.setHwEta(finalMuon.getEta());

    candidate.setHwSign(finalMuon.getSign());
    candidate.setHwSignValid(1);

    candidate.setHwQual(finalMuon.getQuality());

    std::map<int, int> trackAddr;
    trackAddr[0] = finalMuon.getAlgoMuon()->getFiredLayerBits();
    //TODO in the hardware, the uPt is sent to the uGMT at the trackAddr = (uPt << 18) + trackAddr;
    //check if it matters if it needs to be here as well
    trackAddr[1] = finalMuon.getAlgoMuon()->getRefLayer();
    trackAddr[2] = finalMuon.getAlgoMuon()->getDisc();
    if (candidate.hwPt() > 0 || candidate.hwPtUnconstrained() > 0) {
      candidate.setTrackAddress(trackAddr);
      candidate.setTFIdentifiers(iProcessor, mtfType);
      result.push_back(candidate);
    }
  }

  return result;
}

///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
template <class GoldenPatternType>
bool OMTFProcessor<GoldenPatternType>::checkHitPatternValidity(unsigned int hits) {
  ///FIXME: read the list from configuration so this can be controlled at runtime.
  std::vector<unsigned int> badPatterns = {
      99840, 34304, 3075, 36928, 12300, 98816, 98944, 33408, 66688, 66176, 7171, 20528, 33856, 35840, 4156, 34880};

  /*
99840 01100001 1000 000000      011000011000000000
34304 00100001 1000 000000      001000011000000000
 3075 00000011 0000 000011      000000110000000011
36928 00100100 0001 000000      001001000001000000
12300 00001100 0000 001100      000011000000001100
98816 01100000 1000 000000      011000001000000000
98944 01100000 1010 000000      011000001010000000
33408 00100000 1010 000000      001000001010000000
66688 01000001 0010 000000      010000010010000000
66176 01000000 1010 000000      010000001010000000
 7171 00000111 0000 000011      000001110000000011
20528 00010100 0000 110000      000101000000110000
33856 00100001 0001 000000      001000010001000000
35840 00100011 0000 000000      001000110000000000
 4156 00000100 0000 111100      000001000000111100
34880 00100010 0001 000000      001000100001000000
   */
  for (auto aHitPattern : badPatterns) {
    if (hits == aHitPattern)
      return false;
  }

  return true;
}
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
template <class GoldenPatternType>
AlgoMuons OMTFProcessor<GoldenPatternType>::sortResults(unsigned int iProcessor, l1t::tftype mtfType, int charge) {
  unsigned int procIndx = this->myOmtfConfig->getProcIndx(iProcessor, mtfType);
  return sorter->sortResults(procIndx, this->getPatterns(), charge);
}

template <class GoldenPatternType>
int OMTFProcessor<GoldenPatternType>::extrapolateDtPhiBFloatPoint(const int& refLogicLayer,
                                                                  const int& refPhi,
                                                                  const int& refPhiB,
                                                                  const int& refHitSuperLayer,
                                                                  unsigned int targetLayer,
                                                                  const int& targetStubPhi,
                                                                  const int& targetStubQuality,
                                                                  const int& targetStubEta,
                                                                  const int& targetStubR,
                                                                  const OMTFConfiguration* omtfConfig) {
  LogTrace("l1tOmtfEventPrint") << "\n"
                                << __FUNCTION__ << ":" << __LINE__ << " refLogicLayer " << refLogicLayer
                                << " refHitSuperLayer " << refHitSuperLayer << " targetLayer " << targetLayer
                                << std::endl;
  LogTrace("l1tOmtfEventPrint") << "refPhi " << refPhi << " refPhiB " << refPhiB << " targetStubPhi " << targetStubPhi
                                << " targetStubQuality " << targetStubQuality << std::endl;

  int phiExtr = 0;  //delta phi extrapolated

  float rRefLayer = 431.133;  //[cm], MB1 i.e. refLogicLayer = 0
  if (refLogicLayer == 2)
    rRefLayer = 512.401;  //MB2
  else if (refLogicLayer != 0) {
    return 0;
    //throw cms::Exception("OMTFProcessor<GoldenPatternType>::extrapolateDtPhiB: wrong refStubLogicLayer " + std::to_string(refLogicLayer) );
  }

  int reflLayerIndex = refLogicLayer == 0 ? 0 : 1;
  if (useStubQualInExtr) {
    //the phase-2 DT Trigger Primitives, since CMSSW_14_2_0_pre1 define phi always in "the middle of the chamber"
    //also for the uncorrelated stubs
    //so the below correction has sense only of the phase-1
    if (refHitSuperLayer == 1) {
      rRefLayer = rRefLayer - 23.5 / 2;  //inner superlayer
    } else if (refHitSuperLayer == 3) {  //using refHitSuperLayer = 3 here as in the L1Phase2MuDTPhDigi::slNum()
      rRefLayer = rRefLayer + 23.5 / 2;  //inner superlayer
    }

    reflLayerIndex = (refHitSuperLayer << 1) | reflLayerIndex;
  }

  if (targetLayer == 0 || targetLayer == 2 || targetLayer == 4 || (targetLayer >= 10 && targetLayer <= 14)) {
    //all units are cm. Values from the CMS geometry
    float rTargetLayer = 512.475;  //MB2

    if (targetLayer == 0)
      rTargetLayer = 431.175;     //MB1
    else if (targetLayer == 4) {  //MB3
      //it is different than in the phase-1, as in the phase-2 it is a middle of the DT chamber, not muon station
      if (omtfConfig->usePhase2DTPrimitives())
        rTargetLayer = 619.675;
      else
        rTargetLayer = 617.946;
    }

    else if (targetLayer == 10)
      rTargetLayer = 413.675;  //RB1in
    else if (targetLayer == 11)
      rTargetLayer = 448.675;  //RB1out
    else if (targetLayer == 12)
      rTargetLayer = 494.975;  //RB2in
    else if (targetLayer == 13)
      rTargetLayer = 529.975;  //RB2out
    else if (targetLayer == 14)
      rTargetLayer = 602.150;  //RB3

    if (useStubQualInExtr) {
      if (targetLayer == 0 || targetLayer == 2 || targetLayer == 4) {
        if (targetStubQuality == 2 || targetStubQuality == 0)
          rTargetLayer = rTargetLayer - 23.5 / 2;  //inner superlayer
        else if (targetStubQuality == 3 || targetStubQuality == 1)
          rTargetLayer = rTargetLayer + 23.5 / 2;  //outer superlayer
      }
    }

    float d = rTargetLayer - rRefLayer;
    //formula in the form as in the slides explaining the extrapolation algorithm
    //float deltaPhiExtr = d/rTargetLayer * refPhiB / omtfConfig->dtPhiBUnitsRad(); //[rad]
    //phiExtr = round(deltaPhiExtr / omtfConfig->omtfPhiUnit()); //[halfStrip]

    //formula with approximation, used to calculate extrFactor
    float extrFactor = d / rTargetLayer / omtfConfig->dtPhiBUnitsRad() / omtfConfig->omtfPhiUnit();
    phiExtr = extrFactor * (float)refPhiB;  //[halfStrip]

    //formula without approximation
    float deltaPhiExtr = atan(d / rTargetLayer * tan(refPhiB / omtfConfig->dtPhiBUnitsRad()));  //[rad]
    phiExtr = round(deltaPhiExtr / omtfConfig->omtfPhiUnit());                                  //[halfStrip]

    if (useStubQualInExtr & (targetLayer == 0 || targetLayer == 2 || targetLayer == 4)) {
      extrapolFactors[reflLayerIndex][targetLayer][targetStubQuality] = extrFactor;
      extrapolFactorsNorm[reflLayerIndex][targetLayer][targetStubQuality] = 1;
    } else {
      extrapolFactors[reflLayerIndex][targetLayer][0] = extrFactor;
      extrapolFactorsNorm[reflLayerIndex][targetLayer][0] = 1;
    }

    //LogTrace("l1tOmtfEventPrint") <<__FUNCTION__<<":"<<__LINE__<<" deltaPhiExtr "<<deltaPhiExtr<<" phiExtr "<<phiExtr<<std::endl;

    LogTrace("l1tOmtfEventPrint") << "\n"
                                  << __FUNCTION__ << ":" << __LINE__ << " refLogicLayer " << refLogicLayer
                                  << " targetLayer " << std::setw(2) << targetLayer << " targetStubQuality "
                                  << targetStubQuality << " extrFactor " << extrFactor << std::endl;

    LogTrace("l1tOmtfEventPrint") << __FUNCTION__ << ":" << __LINE__ << " refPhiB " << refPhiB << " phiExtr " << phiExtr
                                  << std::endl;

  } else if (targetLayer == 1 || targetLayer == 3 || targetLayer == 5) {
    int deltaPhi = targetStubPhi - refPhi;  //[halfStrip]

    //deltaPhi is here in phi_b hw scale
    deltaPhi = round(deltaPhi * omtfConfig->omtfPhiUnit() * omtfConfig->dtPhiBUnitsRad());

    phiExtr = refPhiB - deltaPhi;  //phiExtr is also in phi_b hw scale
    LogTrace("l1tOmtfEventPrint") << __FUNCTION__ << ":" << __LINE__ << " deltaPhi " << deltaPhi << " phiExtr "
                                  << phiExtr << std::endl;
  } else if ((targetLayer >= 6 && targetLayer <= 9) || (targetLayer >= 15 && targetLayer <= 17)) {
    //if true, for the CSC and endcap RPC the R is taken from the hit coordinates

    float rME = targetStubR;
    if (!useEndcapStubsRInExtr) {
      //all units are cm. This are the average R values for a given chamber (more or less middle of the chamber, but taking into account the OMTF eta range)
      if (targetLayer == 6 || targetLayer == 15)  //ME1/3, RE1/3,
        rME = 600.;
      else if (targetLayer == 7 || targetLayer == 15) {  //ME2/2, RE2/3,
        if (refLogicLayer == 0)
          rME = 600.;
        else
          rME = 640.;
      } else if (targetLayer == 8 || rME == 16) {  //ME3/2, RE3/3,
        if (refLogicLayer == 0)
          rME = 620.;
        else
          rME = 680.;
      } else if (targetLayer == 9) {
        rME = 460.;  //for the refLogicLayer = 1. refLogicLayer = 2 is impossible
      }
    }

    float d = rME - rRefLayer;
    //formula in the form as in the slides explaining the extrapolation algorithm
    //float deltaPhiExtr = d / rME * refPhiB / omtfConfig->dtPhiBUnitsRad();  //[rad]
    //phiExtr = round(deltaPhiExtr / omtfConfig->omtfPhiUnit()); //[halfStrip]

    //formula with approximation, used to calculate extrFactor
    float extrFactor = d / rME / omtfConfig->dtPhiBUnitsRad() / omtfConfig->omtfPhiUnit();
    phiExtr = extrFactor * refPhiB;  //[halfStrip]

    //formula without approximation
    float deltaPhiExtr = atan(d / rME * tan(refPhiB / omtfConfig->dtPhiBUnitsRad()));  //[rad]
    phiExtr = round(deltaPhiExtr / omtfConfig->omtfPhiUnit());                         //[halfStrip]

    if (useEndcapStubsRInExtr) {
      //extrapolFactors[reflLayerIndex][targetLayer][std::abs(targetStubEta)] += extrFactor;
      //extrapolFactorsNorm[reflLayerIndex][targetLayer][std::abs(targetStubEta)]++;
      extrapolFactors[reflLayerIndex][targetLayer][std::abs(rME)] += extrFactor;
      extrapolFactorsNorm[reflLayerIndex][targetLayer][std::abs(rME)]++;
      //extrapolFactors[reflLayerIndex][targetLayer][0] += extrFactor;
      //extrapolFactorsNorm[reflLayerIndex][targetLayer][0]++;
    } else {
      extrapolFactors[reflLayerIndex][targetLayer][0] = extrFactor;
      extrapolFactorsNorm[reflLayerIndex][targetLayer][0] = 1;
    }
    LogTrace("l1tOmtfEventPrint") << "\n"
                                  << __FUNCTION__ << ":" << __LINE__ << " refLogicLayer " << refLogicLayer
                                  << " targetLayer " << std::setw(2) << targetLayer << " targetStubR " << targetStubR
                                  << " targetStubEta " << targetStubEta << " extrFactor "
                                  << " rRefLayer " << rRefLayer << " d " << d << " deltaPhiExtr " << deltaPhiExtr
                                  << " phiExtr " << phiExtr << std::endl;
  }
  //TODO restrict the range of the phiExtr and refPhiB !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  return phiExtr;
}

template <class GoldenPatternType>
int OMTFProcessor<GoldenPatternType>::extrapolateDtPhiBFixedPoint(const int& refLogicLayer,
                                                                  const int& refPhi,
                                                                  const int& refPhiB,
                                                                  const int& refHitSuperLayer,
                                                                  unsigned int targetLayer,
                                                                  const int& targetStubPhi,
                                                                  const int& targetStubQuality,
                                                                  const int& targetStubEta,
                                                                  const int& targetStubR,
                                                                  const OMTFConfiguration* omtfConfig) {
  int phiExtr = 0;  //delta phi extrapolated

  int reflLayerIndex = refLogicLayer == 0 ? 0 : 1;

  if (useStubQualInExtr)
    reflLayerIndex = (refHitSuperLayer << 1) | reflLayerIndex;

  int extrFactor = 0;

  // ==========================================================================
  // DEBUG: Print configuration ONCE
  // ==========================================================================
  static bool config_printed = false;
  if (!config_printed) {
    std::cout << "\n========================================================================\n";
    std::cout << "  FIXED-POINT EXTRAPOLATION CONFIG (for HLS compatibility)\n";
    std::cout << "========================================================================\n";
    std::cout << "nProcessors    : " << omtfConfig->nProcessors() << "\n";
    std::cout << "nLayers        : " << omtfConfig->nLayers() << "\n";
    std::cout << "nPhiBins       : " << omtfConfig->nPhiBins() << " (GP_N_OF_PHI_BINS)\n";
    std::cout << "omtfPhiUnit()  : " << std::setprecision(12) << omtfConfig->omtfPhiUnit() << "\n";
    std::cout << "dtPhiBUnitsRad(): " << std::setprecision(12) << omtfConfig->dtPhiBUnitsRad() << "\n";

    double omtfPhiUnit_val = omtfConfig->omtfPhiUnit();
    double dtPhiBUnitsRad_val = omtfConfig->dtPhiBUnitsRad();
    double scaleFactor_fp = omtfPhiUnit_val * dtPhiBUnitsRad_val * 512.0;
    int scaleFactor_int = (int)scaleFactor_fp;

    std::cout << "\nScaleFactor (for layers 1,3,5):\n";
    std::cout << "  Floating-point: " << std::setprecision(12) << scaleFactor_fp << "\n";
    std::cout << "  Integer (cast): " << scaleFactor_int << "\n";
    std::cout << "  Expected Phase-1: ~305,  Phase-2: ~610\n";
    std::cout << "extrapolMultiplier: " << extrapolMultiplier << "\n";
    std::cout << "========================================================================\n\n";
    config_printed = true;
  }
  // ==========================================================================

  if (targetLayer == 0 || targetLayer == 2 || targetLayer == 4) { //non-bending layers
    if (useStubQualInExtr)
      extrFactor = extrapolFactors[reflLayerIndex][targetLayer][targetStubQuality];
    else
      extrFactor = extrapolFactors[reflLayerIndex][targetLayer][0];
  } else if (targetLayer == 1 || targetLayer == 3 || targetLayer == 5) { //bending layers
    int deltaPhi = targetStubPhi - refPhi;  //here targetStubPhi is phi, not phiB

    int scaleFactor = this->myOmtfConfig->omtfPhiUnit() * this->myOmtfConfig->dtPhiBUnitsRad() * 512;
    //= 305 for phase-1, 512 is multiplier so that scaleFactor is non-zero integer

    deltaPhi = (deltaPhi * scaleFactor) / 512;  //here deltaPhi is converted to the phi_b hw scale

    phiExtr = refPhiB - deltaPhi;  //phiExtr is also in phi_b hw scale
    //LogTrace("l1tOmtfEventPrint") <<__FUNCTION__<<":"<<__LINE__<<" deltaPhi "<<deltaPhi<<" phiExtr "<<phiExtr<<std::endl;

  } else if (targetLayer >= 10 && targetLayer <= 14) {
    extrFactor = extrapolFactors[reflLayerIndex][targetLayer][0];
  } else if ((targetLayer >= 6 && targetLayer <= 9) || (targetLayer >= 15 && targetLayer <= 17)) {
    if (useEndcapStubsRInExtr) {
      //if given abs(targetStubEta) value is not present in the map, it is added with default value of 0
      //so it should be good. The only problem is that the map can grow...
      //TODO change to targetStubR when it is implemented in the FW
      //extrFactor = extrapolFactors[reflLayerIndex][targetLayer][abs(targetStubEta)];
      extrFactor = extrapolFactors[reflLayerIndex][targetLayer][abs(targetStubR)];
      LogTrace("l1tOmtfEventPrint") << __FUNCTION__ << ":" << __LINE__ << " CSC/RPC lookup with R: reflLayerIndex=" 
                                    << reflLayerIndex << " targetLayer=" << targetLayer << " R=" << abs(targetStubR)
                                    << " extrFactor=" << extrFactor << std::endl;
    } else {
      // Use eta as key when useEndcapStubsRInExtr is false (matches XML KeyType="eta")
      extrFactor = extrapolFactors[reflLayerIndex][targetLayer][abs(targetStubEta)];
      LogTrace("l1tOmtfEventPrint") << __FUNCTION__ << ":" << __LINE__ << " CSC/RPC lookup with ETA: reflLayerIndex=" 
                                    << reflLayerIndex << " targetLayer=" << targetLayer << " eta=" << abs(targetStubEta)
                                    << " extrFactor=" << extrFactor << std::endl;
    }
  }

  if (this->myOmtfConfig->isBendingLayer(targetLayer) == false) {
    phiExtr = extrFactor * refPhiB / extrapolMultiplier;
  }

  LogTrace("l1tOmtfEventPrint") << "\n"
                                << __FUNCTION__ << ":" << __LINE__ << " refLogicLayer " << refLogicLayer
                                << " targetLayer " << targetLayer << std::endl;
  LogTrace("l1tOmtfEventPrint") << "refPhi " << refPhi << " refPhiB " << refPhiB << " targetStubPhi " << targetStubPhi
                                << " targetStubQuality " << targetStubQuality << " targetStubEta " << targetStubEta
                                << " extrFactor " << extrFactor << " phiExtr " << phiExtr << std::endl;

  return phiExtr;
}

template <class GoldenPatternType>
int OMTFProcessor<GoldenPatternType>::extrapolateDtPhiB(const MuonStubPtr& refStub,
                                                        const MuonStubPtr& targetStub,
                                                        unsigned int targetLayer,
                                                        const OMTFConfiguration* omtfConfig) {
  //0 is correlated segment, so middle of the chamber
  // 1 is inner SL, 2 is outer.
  //N.B. that in L1Phase2MuDTPhDigi::slNum() out SL is 3
  int refHitSuperLayer = 0;
  if (refStub->qualityHw == 2 || refStub->qualityHw == 0)
    refHitSuperLayer = 1;
  else if (refStub->qualityHw == 3 || refStub->qualityHw == 1)
    refHitSuperLayer = 2;

  if (useFloatingPointExtrapolation)
    return OMTFProcessor<GoldenPatternType>::extrapolateDtPhiBFloatPoint(refStub->logicLayer,
                                                                         refStub->phiHw,
                                                                         refStub->phiBHw,
                                                                         refHitSuperLayer,
                                                                         targetLayer,
                                                                         targetStub->phiHw,
                                                                         targetStub->qualityHw,
                                                                         targetStub->etaHw,
                                                                         targetStub->r,
                                                                         omtfConfig);
  return OMTFProcessor<GoldenPatternType>::extrapolateDtPhiBFixedPoint(refStub->logicLayer,
                                                                       refStub->phiHw,
                                                                       refStub->phiBHw,
                                                                       refHitSuperLayer,
                                                                       targetLayer,
                                                                       targetStub->phiHw,
                                                                       targetStub->qualityHw,
                                                                       targetStub->etaHw,
                                                                       targetStub->r,
                                                                       omtfConfig);
}

///////////////////////////////////////////////
// Helper functions for outputSlot calculation
///////////////////////////////////////////////

// Get the number of inputs per sector for a given layer
// Returns 2 for DT/CSC/RB3/RPCe, 4 for RB1/RB2
unsigned int getInputsPerSector(unsigned int layer) {
  // Layers 0-5: DT -> 2 inputs per sector
  // Layers 6-9: CSC -> 2 inputs per sector  
  // Layers 10-11: RB1in, RB1out -> 4 inputs per sector
  // Layers 12-13: RB2in, RB2out -> 4 inputs per sector
  // Layer 14: RB3 -> 2 inputs per sector
  // Layers 15-17: RPCe -> 2 inputs per sector
  
  if (layer >= 10 && layer <= 13) {
    return 4;  // RB1 and RB2
  }
  return 2;  // DT, CSC, RB3, RPCe
}

// Get the batch size (depth per channel) for a given layer
// Returns 2 for DT/CSC/RB3/RPCe, 4 for RPC Barrel (RB1/RB2)
unsigned int getBatchSize(unsigned int layer) {
  // Layers 0-5: DT -> batch size 2
  // Layers 6-9: CSC -> batch size 2
  // Layers 10-13: RPC Barrel (RB1, RB2) -> batch size 4
  // Layer 14: RB3 -> batch size 2
  // Layers 15-17: RPC Endcap -> batch size 2
  
  if (layer >= 10 && layer <= 13) {
    return 4;  // RPC Barrel (RB1in, RB1out, RB2in, RB2out)
  }
  return 2;  // DT, CSC, RB3, RPC Endcap
}

// Get the physical channel from a stub (sector_wrapped or chamber_wrapped)
int getPhysicalChannel(const MuonStubPtr& stub, int sector_wrapped, int chamber_wrapped) {
  // Return whichever one is not -1
  if (sector_wrapped != -1) {
    return sector_wrapped;
  } else if (chamber_wrapped != -1) {
    return chamber_wrapped;
  }
  return -1;
}

// Calculate outputSlot for a stub
// Returns -1 if calculation fails (stub should be skipped)
int calculateOutputSlot(unsigned int iProcessor,
                       unsigned int iRegion, 
                       unsigned int iLayer,
                       unsigned int inputNumber,
                       int sector_wrapped,
                       int chamber_wrapped,
                       const OMTFConfiguration* omtfConfig,
                       std::map<std::tuple<unsigned int, unsigned int, int>, int>& batchIndexMap) {
  // Get connection info: pair<iFirstInput, nInputs>
  auto& connections = omtfConfig->getConnections();
  if (iProcessor >= connections.size() || 
      iRegion >= connections[iProcessor].size() || 
      iLayer >= connections[iProcessor][iRegion].size()) {
    return -1;  // Out of bounds
  }
  
  auto& conn = connections[iProcessor][iRegion][iLayer];
  unsigned int firstInput = conn.first;
  unsigned int nInputs = conn.second;
  
  // For DT/RPCb, use sector_wrapped as the physical channel
  // For CSC/RPCe, use chamber_wrapped as the physical channel
  // This ensures bending layers (which share the same sector/chamber) get the same slot
  int physChannel = (sector_wrapped != -1) ? sector_wrapped : chamber_wrapped;
  if (physChannel == -1) {
    std::cout << "DEBUG outputSlot=-1: NO physChannel (both sector_wrapped and chamber_wrapped are -1) "
              << "iProc=" << iProcessor << " iReg=" << iRegion << " iLay=" << iLayer << std::endl;
    return -1;  // No valid physical channel
  }
  
  // Calculate selected channels using Python algorithm
  unsigned int inputsPerSector = getInputsPerSector(iLayer);
  unsigned int firstChannel = firstInput / inputsPerSector;
  unsigned int lastInput = firstInput + nInputs - 1;
  unsigned int lastChannel = lastInput / inputsPerSector;
  
  // Build set of selected channels
  std::vector<unsigned int> selectedChannels;
  for (unsigned int ch = firstChannel; ch <= lastChannel; ch++) {
    selectedChannels.push_back(ch);
  }
  
  // Find channel index (ci) - position of physChannel in selectedChannels
  int channelIndex = -1;
  for (size_t i = 0; i < selectedChannels.size(); i++) {
    if (selectedChannels[i] == static_cast<unsigned int>(physChannel)) {
      channelIndex = static_cast<int>(i);
      break;
    }
  }
  
  if (channelIndex == -1) {
    std::cout << "DEBUG outputSlot=-1: physChannel=" << physChannel 
              << " NOT in selectedChannels [";
    for (size_t i = 0; i < selectedChannels.size(); i++) {
      std::cout << selectedChannels[i];
      if (i < selectedChannels.size()-1) std::cout << ",";
    }
    std::cout << "] iProc=" << iProcessor << " iReg=" << iRegion << " iLay=" << iLayer
              << " firstIn=" << firstInput << " nIn=" << nInputs << std::endl;
    return -1;  // Physical channel not in selected channels
  }
  
  // Get or increment batch index (bi)
  auto key = std::make_tuple(iRegion, iLayer, physChannel);
  int batchIndex = batchIndexMap[key]++;
  
  // Calculate outputSlot = ci * BATCH + bi
  unsigned int batchSize = getBatchSize(iLayer);
  int outputSlot = channelIndex * batchSize + batchIndex;
  
  // Verify bounds: maximum is for RPC Barrel which has most channels/depth
  // Maximum outputSlot should be less than nInputs (which is the total capacity for this region/layer)
  if (outputSlot >= static_cast<int>(nInputs)) {
    std::cout << "DEBUG outputSlot=-1 BOUNDS: slot=" << outputSlot 
              << " >= nIn=" << nInputs
              << " iProc=" << iProcessor << " iReg=" << iRegion << " iLay=" << iLayer
              << " physCh=" << physChannel << " ci=" << channelIndex
              << " bi=" << batchIndex << " BATCH=" << batchSize << std::endl;
    return -1;  // Out of bounds
  }
  
  return outputSlot;
}

///////////////////////////////////////////////
///////////////////////////////////////////////
//const std::vector<OMTFProcessor::resultsMap> &
template <class GoldenPatternType>
void OMTFProcessor<GoldenPatternType>::processInput(unsigned int iProcessor,
                                                    l1t::tftype mtfType,
                                                    const OMTFinput& aInput,
                                                    std::vector<std::unique_ptr<IOMTFEmulationObserver> >& observers,
                                                    XmlIOCache& xmlCache) {
  unsigned int procIndx = this->myOmtfConfig->getProcIndx(iProcessor, mtfType);
  for (auto& itGP : this->theGPs) {
    for (auto& result : itGP->getResults()[procIndx]) {
      result.reset();
    }
  }

  LogTrace("l1tOmtfEventPrint") << __FUNCTION__ << "\n"
                                << __LINE__ << " iProcessor " << iProcessor << " mtfType " << mtfType << " procIndx "
                                << procIndx << " ----------------------" << std::endl;
  //////////////////////////////////////
  //////////////////////////////////////
  std::vector<const RefHitDef*> refHitDefs;

  {
    auto refHitsBits = aInput.getRefHits(iProcessor);
    if (refHitsBits.none())
      return;  // myResults;

    //loop over all possible refHits, e.g. 128
    for (unsigned int iRefHit = 0; iRefHit < this->myOmtfConfig->nRefHits(); ++iRefHit) {
      if (!refHitsBits[iRefHit])
        continue;

      refHitDefs.push_back(&(this->myOmtfConfig->getRefHitsDefs()[iProcessor][iRefHit]));

      if (refHitDefs.size() == this->myOmtfConfig->nTestRefHits())
        break;
    }
  }

  boost::property_tree::ptree procDataTree;
  boost::property_tree::ptree refHitsDataTree; // Separate tree for reference hits data
  LogTrace("l1tOmtfEventPrint") << __FUNCTION__ << " " << __LINE__ << std::endl;
  
  // Track ordering for reference hits per chamber/sector (based on wrapped value)
  // Key format: "sector_X_layer_Y" or "chamber_X_layer_Y" where X is wrapped value and Y is logicLayer
  std::map<std::string, int> refHitWrappedOrder;
  
  // Track batch index for outputSlot calculation
  // Key: tuple<iRegion, iLayer, physicalChannel>, Value: batch index counter
  std::map<std::tuple<unsigned int, unsigned int, int>, int> batchIndexMap;
  
  // New: Collect reference hits data for HLS export BEFORE the main processing loops
  // This creates one row per reference hit with all layer data combined
  for (unsigned int iRefHit = 0; iRefHit < refHitDefs.size(); iRefHit++) {
    // Reset stub_order counter for each reference hit
    refHitWrappedOrder.clear();
    // Reset batch index map for each reference hit
    batchIndexMap.clear();
    
    const RefHitDef& aRefHitDef = *(refHitDefs[iRefHit]);
    unsigned int iRegion = aRefHitDef.iRegion;
    
    // Get reference stub for extrapolation calculations
    unsigned int refLayerLogicNum = this->myOmtfConfig->getRefToLogicNumber()[aRefHitDef.iRefLayer];
    const MuonStubPtr refStub = aInput.getMuonStub(refLayerLogicNum, aRefHitDef.iInput);

    // Mark this stub as a reference in the XmlIOCache
    if (refStub) {
      xmlCache.markReference(iProcessor, omtf::makeStubKey(*refStub));
    }
    
    // Collect restricted stubs from all layers for this reference hit
    std::vector<std::pair<unsigned int, MuonStubPtrs1D>> allLayerStubs;
    std::vector<std::pair<unsigned int, std::vector<int>>> allLayerExtrapolatedPhi;
    std::vector<std::pair<unsigned int, std::vector<unsigned int>>> allLayerHwNumbers; // Hardware layer numbers
    
    for (unsigned int iLayer = 0; iLayer < this->myOmtfConfig->nLayers(); ++iLayer) {
      MuonStubPtrs1D restrictedLayerStubs = this->restrictInput(iProcessor, iRegion, iLayer, aInput);
      
      // Calculate extrapolated phi for each restricted stub in this layer
      std::vector<int> extrapolatedPhi(restrictedLayerStubs.size(), 0);
      
      // Get hardware layer numbers for each stub in this layer
      std::vector<unsigned int> hwNumbers;
      for (auto& stub : restrictedLayerStubs) {
        if (stub) {
          // Use the current iLayer to get the hwNumber, not the stub's original logicLayer
          // This is important because DT stubs can be used in multiple layers
          auto& logicToHwMap = this->myOmtfConfig->getLogicToHwLayer();
          auto hwIt = logicToHwMap.find(iLayer);
          unsigned int hwNumber = (hwIt != logicToHwMap.end()) ? hwIt->second : 0;
          hwNumbers.push_back(hwNumber);
        } else {
          hwNumbers.push_back(0); // Default for null stubs
        }
      }
      
      //TODO make sure the that the iRefLayer numbers used here corresponds to this in the hwToLogicLayer_0x000X.xml
      if ((this->myOmtfConfig->usePhiBExtrapolationMB1() && aRefHitDef.iRefLayer == 0) ||
          (this->myOmtfConfig->usePhiBExtrapolationMB2() && aRefHitDef.iRefLayer == 2)) {
        if ((iLayer != refLayerLogicNum) && (iLayer != refLayerLogicNum + 1)) {
          unsigned int iStub = 0;
          for (auto& targetStub : restrictedLayerStubs) {
            if (targetStub) {
              extrapolatedPhi[iStub] = extrapolateDtPhiB(refStub, targetStub, iLayer, this->myOmtfConfig);
            }
            iStub++;
          }
        }
      }
      
      allLayerStubs.emplace_back(iLayer, restrictedLayerStubs);
      allLayerExtrapolatedPhi.emplace_back(iLayer, extrapolatedPhi);
      allLayerHwNumbers.emplace_back(iLayer, hwNumbers);
    }
    
    // Notify DetailedDebugExporter about restricted stubs for this refHit
    for (auto& obs : observers) {
      if (auto* detailedDebug = dynamic_cast<DetailedDebugExporter*>(obs.get())) {
        // Collect all layers into a 2D structure for the observer
        MuonStubPtrs2D restrictedStubs2D(this->myOmtfConfig->nLayers());
        for (const auto& layerPair : allLayerStubs) {
          restrictedStubs2D[layerPair.first] = layerPair.second;
        }
        detailedDebug->observeRestrictedStubs(iProcessor, iRefHit, aRefHitDef.iRefLayer, 
                                               restrictedStubs2D, allLayerExtrapolatedPhi, refStub);
      }
    }
    
    // === ADD REFERENCE HIT DATA TO XML ===
    auto& refHitTree = refHitsDataTree.add_child("referenceHit", boost::property_tree::ptree());
    refHitTree.add("<xmlattr>.iRefHit", iRefHit);
    refHitTree.add("<xmlattr>.iRefLayer", aRefHitDef.iRefLayer);
    refHitTree.add("<xmlattr>.iRegion", iRegion);
    refHitTree.add("<xmlattr>.iInput", aRefHitDef.iInput);
    
    // Add reference stub data
    if (refStub) {
      refHitTree.add("<xmlattr>.refPhi", refStub->phiHw);
      refHitTree.add("<xmlattr>.refPhiB", refStub->phiBHw);
      refHitTree.add("<xmlattr>.refEta", refStub->etaHw);
      refHitTree.add("<xmlattr>.refQuality", refStub->qualityHw);
      refHitTree.add("<xmlattr>.refLogicLayer", refStub->logicLayer);
      
      // Add sector_wrapped and chamber_wrapped based on detector type using helper functions
      int sectorWrapped = -1;
      int chamberWrapped = -1;
      
      if (refStub->type == MuonStub::DT_PHI || refStub->type == MuonStub::DT_THETA || 
          refStub->type == MuonStub::DT_PHI_ETA || refStub->type == MuonStub::DT_HIT) {
        // DT stub: use helper function
        DTChamberId dtId(refStub->detId);
        sectorWrapped = calculateDTSectorWrapped(dtId, iProcessor, this->myOmtfConfig);
        chamberWrapped = -1;  // Not applicable for DT
      } 
      else if (refStub->type == MuonStub::CSC_PHI || refStub->type == MuonStub::CSC_ETA || 
               refStub->type == MuonStub::CSC_PHI_ETA) {
        // CSC stub: use helper function
        CSCDetId cscId(refStub->detId);
        chamberWrapped = calculateCSCChamberWrapped(cscId, iProcessor, mtfType, this->myOmtfConfig);
        sectorWrapped = -1;  // Not applicable for CSC
      }
      else if (refStub->type == MuonStub::RPC) {
        // RPC stub: use appropriate helper function based on barrel vs endcap
        RPCDetId rpcId(refStub->detId);
        
        if (rpcId.region() == 0) {
          // Barrel RPC: use sector_wrapped helper
          sectorWrapped = calculateRPCSectorWrapped(rpcId, iProcessor, this->myOmtfConfig);
          chamberWrapped = -1;  // Not applicable for barrel RPC
        } else {
          // Endcap RPC: use chamber_wrapped helper
          chamberWrapped = calculateRPCEndcapChamberWrapped(rpcId, iProcessor, this->myOmtfConfig);
          sectorWrapped = -1;  // Not applicable for endcap RPC
        }
      }
      
      // Add the calculated wrapped values to the XML
      refHitTree.add("<xmlattr>.sector_wrapped", sectorWrapped);
      refHitTree.add("<xmlattr>.chamber_wrapped", chamberWrapped);
      
      // Add order within the same chamber/sector AND refLogicLayer (based on wrapped value + layer)
      std::string wrappedKey;
      if (sectorWrapped != -1) {
        wrappedKey = "sector_" + std::to_string(sectorWrapped) + "_layer_" + std::to_string(aRefHitDef.iRefLayer);
      } else if (chamberWrapped != -1) {
        wrappedKey = "chamber_" + std::to_string(chamberWrapped) + "_layer_" + std::to_string(aRefHitDef.iRefLayer);
      }
      
      int refHitOrder = 0;
      if (!wrappedKey.empty()) {
        refHitOrder = refHitWrappedOrder[wrappedKey]++;
      }
      refHitTree.add("<xmlattr>.refHit_order", refHitOrder);
    }
    
    // Add all layer data for this reference hit (only non-empty stubs)
    // Put stubs directly under referenceHit without layer wrapper
    for (unsigned int layerIdx = 0; layerIdx < allLayerStubs.size(); layerIdx++) {
      unsigned int iLayer = allLayerStubs[layerIdx].first;
      const auto& layerStubs = allLayerStubs[layerIdx].second;
      const auto& layerExtrapolatedPhi = allLayerExtrapolatedPhi[layerIdx].second;
      const auto& layerHwNumbers = allLayerHwNumbers[layerIdx].second;
      
      // Add each non-empty stub directly under referenceHit
      for (unsigned int iStub = 0; iStub < layerStubs.size(); iStub++) {
        const auto& stub = layerStubs[iStub];
        if (stub) { // Only add non-empty stubs
          auto& stubTree = refHitTree.add_child("stub", boost::property_tree::ptree());
          //stubTree.add("<xmlattr>.iStub", iStub);
          stubTree.add("<xmlattr>.iLayer", iLayer);
          
          // Calculate inputNumber from iStub using connections config
          // restrictInput only keeps stubs in range [iStart, iEnd], so:
          // inputNumber = iStart + iStub
          unsigned int iStart = this->myOmtfConfig->getConnections()[iProcessor][iRegion][iLayer].first;
          unsigned int inputNumber = iStart + iStub;
          stubTree.add("<xmlattr>.inputNumber", inputNumber);
          
          // For bending layers, phi should be the phiBHw from the previous layer's stub
          // (restrictInput returns the previous layer's stub for bending layers)
          int phiValue = this->myOmtfConfig->isBendingLayer(iLayer) ? stub->phiBHw : stub->phiHw;
          stubTree.add("<xmlattr>.phi", phiValue);
          stubTree.add("<xmlattr>.phiB", stub->phiBHw);
          stubTree.add("<xmlattr>.eta", stub->etaHw);
          stubTree.add("<xmlattr>.quality", stub->qualityHw);
          stubTree.add("<xmlattr>.r", stub->r);
          stubTree.add("<xmlattr>.logicLayer", stub->logicLayer);
          stubTree.add("<xmlattr>.hwNumber", layerHwNumbers[iStub]);
          stubTree.add("<xmlattr>.extrapolatedPhi", layerExtrapolatedPhi[iStub]);
          stubTree.add("<xmlattr>.timing", stub->timing);
          stubTree.add("<xmlattr>.bx", stub->bx);
          stubTree.add("<xmlattr>.detId", stub->detId);
          stubTree.add("<xmlattr>.type", static_cast<int>(stub->type));
          
          // Add hwName using the hwNumber from layerHwNumbers
          std::string hwName = getHwNameFromHwNumber(layerHwNumbers[iStub]);
          if (!hwName.empty()) {
            stubTree.add("<xmlattr>.hwName", hwName);
          }
          
          // Calculate and add sector_wrapped and chamber_wrapped based on stub type
          int stubSectorWrapped = -1;
          int stubChamberWrapped = -1;
          
          if (stub->type == MuonStub::DT_PHI_ETA || stub->type == MuonStub::DT_HIT) {
            // DT stub: use sector_wrapped
            DTChamberId dtId(stub->detId);
            stubSectorWrapped = calculateDTSectorWrapped(dtId, iProcessor, this->myOmtfConfig);
            stubChamberWrapped = -1;
          } else if (stub->type == MuonStub::CSC_PHI_ETA) {
            // CSC stub: use chamber_wrapped
            CSCDetId cscId(stub->detId);
            stubChamberWrapped = calculateCSCChamberWrapped(cscId, iProcessor, this->myOmtfConfig->nProcessors() == 3 ? l1t::tftype::omtf_pos : l1t::tftype::omtf_neg, this->myOmtfConfig);
            stubSectorWrapped = -1;
          } else if (stub->type == MuonStub::RPC) {
            // RPC stub: depends on region (barrel uses sector_wrapped, endcap uses chamber_wrapped)
            RPCDetId rpcId(stub->detId);
            if (rpcId.region() == 0) {
              // Barrel RPC: use sector_wrapped
              stubSectorWrapped = calculateRPCSectorWrapped(rpcId, iProcessor, this->myOmtfConfig);
              stubChamberWrapped = -1;
            } else {
              // Endcap RPC: use chamber_wrapped
              stubChamberWrapped = calculateRPCEndcapChamberWrapped(rpcId, iProcessor, this->myOmtfConfig);
              stubSectorWrapped = -1;
            }
          }
          
          // Add the wrapped fields to XML
          stubTree.add("<xmlattr>.sector_wrapped", stubSectorWrapped);
          stubTree.add("<xmlattr>.chamber_wrapped", stubChamberWrapped);
          
          // Add stub_order based on (wrapped_value, logicLayer) combination
          // This matches the logic in InputMakerPhase2.cc for standalone stubs
          int wrappedValue = (stubSectorWrapped != -1) ? stubSectorWrapped : stubChamberWrapped;
          std::string stubKey = "wrapped_" + std::to_string(wrappedValue) + "_layer_" + std::to_string(iLayer);
          int stub_order = refHitWrappedOrder[stubKey]++;
          stubTree.add("<xmlattr>.stub_order", stub_order);
          
          // Calculate and add outputSlot
          // Use inputNumber directly - it already accounts for the layer configuration
          // For bending layers, the stub comes from the connected layer and inputNumber
          // is calculated relative to that layer's iFirstInput
          int outputSlot = calculateOutputSlot(iProcessor, iRegion, iLayer, inputNumber,
                                               stubSectorWrapped, stubChamberWrapped,
                                               this->myOmtfConfig, batchIndexMap);
          stubTree.add("<xmlattr>.outputSlot", outputSlot);
        }
      }
    }
    
    // Notify observers with complete reference hit data (all layers combined + extrapolated phi + hw numbers)
    for (auto& obs : observers) {
      if (auto* hlsExporter = dynamic_cast<HLSDigiExporter*>(obs.get())) {
        hlsExporter->observeRefHitProcessing(iProcessor, iRefHit, aRefHitDef, allLayerStubs, allLayerExtrapolatedPhi, allLayerHwNumbers);
      }
    }

    // === ADD REFERENCE HIT TO XMLIOCACHE ===
    omtf::ReferenceHitRecord rhRec;
    rhRec.attrs = refHitTree;

    // Build RestrictedStubRecords from the refHitTree's stub children
    for (const auto& child : refHitTree) {
      if (child.first == "stub") {
        omtf::RestrictedStubRecord rsRec;
        rsRec.attrs = child.second;
        // Extract extrapolatedPhi from attributes
        rsRec.extrapolatedPhi = child.second.get<int>("<xmlattr>.extrapolatedPhi");
        rhRec.restrictedStubs.push_back(rsRec);
      }
    }

    // Note: extrapCalcs will be populated during the second loop below for layers 0 and 2
    xmlCache.addReferenceHit(iProcessor, rhRec);
  }
  
  for (unsigned int iLayer = 0; iLayer < this->myOmtfConfig->nLayers(); ++iLayer) {
    //debug
    /*for(auto& h : layerHits) {
      if(h != 5400)
        LogTrace("l1tOmtfEventPrint")<<__FUNCTION__<<" "<<__LINE__<<" iLayer "<<iLayer<<" layerHit "<<h<<std::endl;
    }*/

    for (unsigned int iRefHit = 0; iRefHit < refHitDefs.size(); iRefHit++) {
      const RefHitDef& aRefHitDef = *(refHitDefs[iRefHit]);

      unsigned int refLayerLogicNum = this->myOmtfConfig->getRefToLogicNumber()[aRefHitDef.iRefLayer];
      const MuonStubPtr refStub = aInput.getMuonStub(refLayerLogicNum, aRefHitDef.iInput);
      //int etaRef = refStub->etaHw;

      unsigned int iRegion = aRefHitDef.iRegion;

      MuonStubPtrs1D restrictedLayerStubs = this->restrictInput(iProcessor, iRegion, iLayer, aInput);

      //LogTrace("l1tOmtfEventPrint")<<__FUNCTION__<<" "<<__LINE__<<" iLayer "<<iLayer<<" iRefLayer "<<aRefHitDef.iRefLayer<<std::endl;
      //LogTrace("l1tOmtfEventPrint")<<"iLayer "<<iLayer<<" iRefHit "<<iRefHit;
      //LogTrace("l1tOmtfEventPrint")<<" nTestedRefHits "<<nTestedRefHits<<" aRefHitDef "<<aRefHitDef<<std::endl;

      std::vector<int> extrapolatedPhi(restrictedLayerStubs.size(), 0);

      //TODO make sure the that the iRefLayer numbers used here corresponds to this in the hwToLogicLayer_0x000X.xml
      if ((this->myOmtfConfig->usePhiBExtrapolationMB1() && aRefHitDef.iRefLayer == 0) ||
          (this->myOmtfConfig->usePhiBExtrapolationMB2() && aRefHitDef.iRefLayer == 2)) {
        if ((iLayer != refLayerLogicNum) && (iLayer != refLayerLogicNum + 1)) {
          unsigned int iStub = 0;
          for (auto& targetStub : restrictedLayerStubs) {
            if (targetStub) {
              extrapolatedPhi[iStub] = extrapolateDtPhiB(refStub, targetStub, iLayer, this->myOmtfConfig);

              LogTrace("l1tOmtfEventPrint")
                  << "\n"
                  << __FUNCTION__ << ":" << __LINE__ << " extrapolating from layer " << refLayerLogicNum
                  << " - iRefLayer " << aRefHitDef.iRefLayer << " to layer " << iLayer << " stub " << targetStub
                  << " value " << extrapolatedPhi[iStub] << std::endl;

              if (this->myOmtfConfig->getDumpResultToXML()) {
                // === ADD TO XMLIOCACHE FOR LAYERS 0 AND 2 ===
                // Build calc record and add to the reference hit's extrapCalcs
                boost::property_tree::ptree calcTree;
                calcTree.add("<xmlattr>.targetLayer", iLayer);
                calcTree.add("<xmlattr>.method", "fixed"); // phiB extrapolation uses fixed-point
                calcTree.add("<xmlattr>.refLogicLayer", refStub->logicLayer);
                calcTree.add("<xmlattr>.refPhi", refStub->phiHw);
                calcTree.add("<xmlattr>.refPhiB", refStub->phiBHw);
                calcTree.add("<xmlattr>.targetStubPhi", targetStub->phiHw);
                calcTree.add("<xmlattr>.targetStubQuality", targetStub->qualityHw);
                calcTree.add("<xmlattr>.targetStubEta", targetStub->etaHw);
                calcTree.add("<xmlattr>.targetStubR", targetStub->r);

                // Calculate and add extrapolation factor
                int reflLayerIndex = refStub->logicLayer == 0 ? 0 : 1;
                int extrFactor = 0;
                
                if (iLayer == 0 || iLayer == 2 || iLayer == 4) { // non-bending layers
                  if (useStubQualInExtr)
                    extrFactor = extrapolFactors[reflLayerIndex][iLayer][targetStub->qualityHw];
                  else
                    extrFactor = extrapolFactors[reflLayerIndex][iLayer][0];
                  calcTree.add("<xmlattr>.extrFactor", extrFactor);
                } else if (iLayer == 1 || iLayer == 3 || iLayer == 5) { // bending layers
                  int scaleFactor = this->myOmtfConfig->omtfPhiUnit() * this->myOmtfConfig->dtPhiBUnitsRad() * 512;
                  int deltaPhi_raw = targetStub->phiHw - refStub->phiHw;
                  int deltaPhi_scaled = (deltaPhi_raw * scaleFactor) / 512;
                  calcTree.add("<xmlattr>.scaleFactor", scaleFactor);
                  calcTree.add("<xmlattr>.deltaPhi_raw", deltaPhi_raw);
                  calcTree.add("<xmlattr>.deltaPhi_scaled", deltaPhi_scaled);
                  // For bending layers, extrFactor is not used (calculation is direct)
                  calcTree.add("<xmlattr>.extrFactor", 0); // N/A for bending layers
                } else if (iLayer >= 10 && iLayer <= 14) {
                  extrFactor = extrapolFactors[reflLayerIndex][iLayer][0];
                  calcTree.add("<xmlattr>.extrFactor", extrFactor);
                } else if ((iLayer >= 6 && iLayer <= 9) || (iLayer >= 15 && iLayer <= 17)) {
                  if (useEndcapStubsRInExtr) {
                    extrFactor = extrapolFactors[reflLayerIndex][iLayer][abs(targetStub->r)];
                  } else {
                    // Use eta as key when useEndcapStubsRInExtr is false (matches XML KeyType="eta")
                    extrFactor = extrapolFactors[reflLayerIndex][iLayer][abs(targetStub->etaHw)];
                  }
                  calcTree.add("<xmlattr>.extrFactor", extrFactor);
                }

                calcTree.add("<xmlattr>.phiExtr", extrapolatedPhi[iStub]);

                xmlCache.addExtrapolationCalc(iProcessor, iRefHit, calcTree);
              }
            }
            iStub++;
          }
        }
      }

      for (auto& itGP : this->theGPs) {
        if (itGP->key().thePt == 0)  //empty pattern
          continue;

        StubResult stubResult =
            itGP->process1Layer1RefLayer(aRefHitDef.iRefLayer, iLayer, restrictedLayerStubs, extrapolatedPhi, refStub);

        /* LogTrace("l1tOmtfEventPrint")<<__FUNCTION__<<":"<<__LINE__
                                     <<" layerResult: valid"<<stubResult.getValid()
                                     <<" pdfVal "<<stubResult.getPdfVal()
                                     <<std::endl;*/

        // Collect GP processing results for CSV export and detailed debug
        for (auto& observer : observers) {
          observer->observeGoldenPatternResults(iProcessor, iRefHit, aRefHitDef,
                                                itGP->key().theNumber, iLayer,
                                                stubResult, stubResult.getPdfBin());

          // Detailed debug export - capture all StubResult data
          if (auto* detailedDebug = dynamic_cast<DetailedDebugExporter*>(observer.get())) {
            detailedDebug->observeStubResult(iProcessor, iRefHit, aRefHitDef.iRefLayer, iLayer,
                                             itGP->key().theNumber, itGP->key(),
                                             stubResult, restrictedLayerStubs, extrapolatedPhi, refStub, itGP.get());
          }
        }

        itGP->getResults()[procIndx][iRefHit].setStubResult(iLayer, stubResult);
      }
    }
  }

  for (unsigned int iRefHit = 0; iRefHit < refHitDefs.size(); iRefHit++) {
    const RefHitDef& aRefHitDef = *(refHitDefs[iRefHit]);

    unsigned int refLayerLogicNum = this->myOmtfConfig->getRefToLogicNumber()[aRefHitDef.iRefLayer];
    const MuonStubPtr refStub = aInput.getMuonStub(refLayerLogicNum, aRefHitDef.iInput);

    int phiRef = refStub->phiHw;
    int etaRef = refStub->etaHw;

    //calculating the phiExtrp in the case the RefLayer is MB1, to include it in the  candidate phi of candidate
    unsigned int layerPhiOut = 2;  //the layer at which the candidate output phi is defined
    unsigned int extrRefLayer = layerPhiOut == 0 ? 2 : 0;
    //N.B. is seems that using layer 0 (MB1) as the layer where the phi is defined gives much worse results - worse phi and more ghosts

    int phiExtrp = 0;
    if ((this->myOmtfConfig->usePhiBExtrapolationMB1() && aRefHitDef.iRefLayer == extrRefLayer)) {
      //||(this->myOmtfConfig->getUsePhiBExtrapolationMB2() && aRefHitDef.iRefLayer == 2) ) {  //the extrapolation from the layer 2 to the layer 2 has no sense, so phiExtrp is 0
      LogTrace("l1tOmtfEventPrint") << "\n"
                                    << __FUNCTION__ << ":" << __LINE__
                                    << "extrapolating ref hit to get the phi of the candidate" << std::endl;
      if (useFloatingPointExtrapolation)
        phiExtrp = extrapolateDtPhiBFloatPoint(
            aRefHitDef.iRefLayer, phiRef, refStub->phiBHw, 0, layerPhiOut, 0, 6, 0, 0, this->myOmtfConfig);
      else
        phiExtrp = extrapolateDtPhiBFixedPoint(
            aRefHitDef.iRefLayer, phiRef, refStub->phiBHw, 0, layerPhiOut, 0, 6, 0, 0, this->myOmtfConfig);
    }

    for (auto& itGP : this->theGPs) {
      if (itGP->key().thePt == 0)  //empty pattern
        continue;

      int phiRefSt2 = itGP->propagateRefPhi(phiRef + phiExtrp, etaRef, aRefHitDef.iRefLayer, layerPhiOut);
      itGP->getResults()[procIndx][iRefHit].set(aRefHitDef.iRefLayer, phiRefSt2, etaRef, phiRef);
    }
  }

  //////////////////////////////////////
  //////////////////////////////////////
  {
    unsigned int iGPIndex = 0;
    for (auto& itGP : this->theGPs) {
      itGP->finalise(procIndx);
      //debug
      /*for(unsigned int iRefHit = 0; iRefHit < itGP->getResults()[procIndx].size(); ++iRefHit) {
        if(itGP->getResults()[procIndx][iRefHit].isValid()) {
          LogTrace("l1tOmtfEventPrint")<<__FUNCTION__<<":"<<"__LINE__"<<itGP->getResults()[procIndx][iRefHit]<<std::endl;
        }
      }*/

      // Collect GP final results for CSV export and detailed debug
      auto gpResults = itGP->getResults()[procIndx];
      for (unsigned int iRefHit = 0; iRefHit < gpResults.size(); iRefHit++) {
        for (auto& observer : observers) {
          observer->observeGoldenPatternFinalResults(iProcessor, iGPIndex, itGP->key(), iRefHit, gpResults[iRefHit]);
        }
      }

      // Detailed debug export - capture finalise results (algoMuons)
      for (auto& observer : observers) {
        if (auto* detailedDebug = dynamic_cast<DetailedDebugExporter*>(observer.get())) {
          detailedDebug->observeFinaliseResults(iProcessor, itGP->key().theNumber, itGP->key(), gpResults);
        }
      }
      
      iGPIndex++;
    }
  }

  // === EMIT UNIFIED XML FROM XMLIOCACHE ===
  auto bucket = xmlCache.get(iProcessor);
  if (bucket) {
    // Build <inputDigis> tree
    boost::property_tree::ptree inputDigisTree;
    for (const auto& digiRec : bucket->digis) {
      boost::property_tree::ptree digiNode;
      // Add type as FIRST attribute
      digiNode.add("<xmlattr>.type", digiRec.type);
      // Copy remaining attributes
      for (const auto& attr : digiRec.attrs.get_child("<xmlattr>")) {
        digiNode.add("<xmlattr>." + attr.first, attr.second.data());
      }
      inputDigisTree.add_child("digi", digiNode);
    }

    // Build <inputStubs> tree
    boost::property_tree::ptree inputStubsTree;
    for (const auto& stubRec : bucket->stubs) {
      boost::property_tree::ptree stubNode;
      // Add type as FIRST attribute
      stubNode.add("<xmlattr>.type", stubRec.type);
      // Add isReference as SECOND attribute
      bool isRef = (bucket->referenceKeys.find(stubRec.key) != bucket->referenceKeys.end());
      stubNode.add("<xmlattr>.isReference", isRef);
      // Copy remaining attributes
      for (const auto& attr : stubRec.attrs.get_child("<xmlattr>")) {
        stubNode.add("<xmlattr>." + attr.first, attr.second.data());
      }

      inputStubsTree.add_child("stub", stubNode);
    }

    // Build <referenceHits> tree
    boost::property_tree::ptree referenceHitsTree;
    for (const auto& rh : bucket->refHits) {
      boost::property_tree::ptree rhNode;

      // Copy refHit attributes (excluding the stub children which we'll rebuild)
      if (rh.attrs.find("<xmlattr>") != rh.attrs.not_found()) {
        for (const auto& attr : rh.attrs.get_child("<xmlattr>")) {
          rhNode.add("<xmlattr>." + attr.first, attr.second.data());
        }
      }

      // Add <extrapolatedPhiCalcs> (currently empty in this implementation, would be populated from procDataTree)
      // TODO: Populate this from the extrapolation loop if needed for detailed calculations
      if (!rh.extrapCalcs.empty()) {
        boost::property_tree::ptree calcsNode;
        for (const auto& calc : rh.extrapCalcs) {
          calcsNode.add_child("calc", calc);
        }
        rhNode.add_child("extrapolatedPhiCalcs", calcsNode);
      }

      // Add <restrictedStubs>
      boost::property_tree::ptree restrictedStubsNode;
      for (const auto& rs : rh.restrictedStubs) {
        restrictedStubsNode.add_child("stub", rs.attrs);
      }
      rhNode.add_child("restrictedStubs", restrictedStubsNode);

      referenceHitsTree.add_child("referenceHit", rhNode);
    }

    // Emit unified XML to observers
    for (auto& obs : observers) {
      obs->addProcesorData("inputDigis", inputDigisTree);
      obs->addProcesorData("inputStubs", inputStubsTree);
      obs->addProcesorData("referenceHits", referenceHitsTree);

      // Keep extrapolation tree for backwards compatibility
      //obs->addProcesorData("extrapolation", procDataTree);
    }
  }

  return;
}
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////

template <class GoldenPatternType>
FinalMuons OMTFProcessor<GoldenPatternType>::run(unsigned int iProcessor,
                                                 l1t::tftype mtfType,
                                                 int bx,
                                                 OMTFinputMaker* inputMaker,
                                                 std::vector<std::unique_ptr<IOMTFEmulationObserver> >& observers,
                                                 XmlIOCache& xmlCache) {
  //uncomment if you want to check execution time of each method
  //boost::timer::auto_cpu_timer t("%ws wall, %us user in getProcessorCandidates\n");

  // Clear cache for this processor before processing
  xmlCache.clearProcessor(iProcessor);

  for (auto& obs : observers)
    obs->observeProcesorBegin(iProcessor, mtfType);

  //input is shared_ptr because the observers may need them after the run() method execution is finished
  std::shared_ptr<OMTFinput> input = std::make_shared<OMTFinput>(this->myOmtfConfig);
  inputMaker->buildInputForProcessor(input->getMuonStubs(), iProcessor, mtfType, bx, bx, observers, xmlCache);

  if (this->myOmtfConfig->cleanStubs()) {
    //this has sense for the pattern generation from the tracks with the secondaries
    //if more than one stub is in a given layer, all stubs are removed from this layer
    for (unsigned int iLayer = 0; iLayer < input->getMuonStubs().size(); ++iLayer) {
      auto& layerStubs = input->getMuonStubs()[iLayer];
      int count = std::count_if(layerStubs.begin(), layerStubs.end(), [](auto& ptr) { return ptr != nullptr; });
      if (count > 1) {
        for (auto& ptr : layerStubs)
          ptr.reset();

        LogTrace("OMTFReconstruction") << __FUNCTION__ << ":" << __LINE__ << "cleaning stubs in the layer " << iLayer
                                       << " stubs count :" << count << std::endl;
      }
    }
  }

  //LogTrace("l1tOmtfEventPrint")<<"buildInputForProce "; t.report();
  processInput(iProcessor, mtfType, *(input.get()), observers, xmlCache);

  //LogTrace("l1tOmtfEventPrint")<<"processInput       "; t.report();
  AlgoMuons algoCandidates = sortResults(iProcessor, mtfType);

  // Collect sorted candidate results for CSV export
  for (auto& observer : observers) {
    observer->observeSortedCandidates(iProcessor, mtfType, algoCandidates);
  }

  if (ptAssignment) {
    for (auto& myCand : algoCandidates) {
      if (myCand->isValid()) {
        ptAssignment->run(myCand, observers);
      }
    }
  }

  //LogTrace("l1tOmtfEventPrint")<<"sortResults        "; t.report();
  // perform GB
  //watch out: etaBits2HwEta is used in the ghostBust to convert the AlgoMuons eta, it affect algoCandidates as they are pointers
  AlgoMuons gbCandidates = ghostBust(algoCandidates);

  //LogTrace("l1tOmtfEventPrint")<<"ghostBust"; t.report();

  FinalMuons finalMuons = convertToOuputScales(iProcessor, mtfType, gbCandidates);

  // fill RegionalMuonCand colleciton
  //std::vector<l1t::RegionalMuonCand> candMuons = getFinalcandidates(iProcessor, mtfType, gbCandidates);

  for (auto& obs : observers) {
    obs->observeProcesorEmulation(iProcessor, mtfType, input, algoCandidates, gbCandidates, finalMuons);
  }

  return finalMuons;
}

template <class GoldenPatternType>
void OMTFProcessor<GoldenPatternType>::printInfo() const {
  edm::LogVerbatim("OMTFReconstruction") << __PRETTY_FUNCTION__ << std::endl;

  ProcessorBase<GoldenPatternType>::printInfo();
}

template <class GoldenPatternType>
void OMTFProcessor<GoldenPatternType>::saveExtrapolFactors() {
  //if(this->myOmtfConfig->nProcessors() == 3) //phase2
  extrapolMultiplier = 512;

  boost::property_tree::ptree tree;
  auto& extrFactorsTree = tree.add("ExtrapolationFactors", "");
  extrFactorsTree.add("<xmlattr>.multiplier", extrapolMultiplier);

  edm::LogVerbatim("OMTFReconstruction") << "saving extrapolFactors to ExtrapolationFactors.xml" << std::endl;
  for (unsigned int iRefLayer = 0; iRefLayer < extrapolFactors.size(); iRefLayer++) {
    for (unsigned int iLayer = 0; iLayer < extrapolFactors[iRefLayer].size(); iLayer++) {
      edm::LogVerbatim("OMTFReconstruction") << " iRefLayer " << iRefLayer << " iLayer " << iLayer << std::endl;

      auto& layerTree = extrFactorsTree.add_child("Lut", boost::property_tree::ptree());
      layerTree.add("<xmlattr>.RefLayer", std::to_string(iRefLayer));
      layerTree.add("<xmlattr>.Layer", iLayer);

      if (useStubQualInExtr && (iLayer == 0 || iLayer == 2 || iLayer == 4))
        layerTree.add("<xmlattr>.KeyType", "quality");
      else if (useEndcapStubsRInExtr && ((iLayer >= 6 && iLayer <= 9) || (iLayer >= 15 && iLayer <= 17)))
        layerTree.add("<xmlattr>.KeyType", "eta");
      else
        layerTree.add("<xmlattr>.KeyType", "none");

      for (auto& extrFactors : extrapolFactors[iRefLayer][iLayer]) {
        int norm = 1;
        if (!extrapolFactorsNorm[iRefLayer][iLayer].empty())
          norm = extrapolFactorsNorm[iRefLayer][iLayer][extrFactors.first];
        auto& lutVal = layerTree.add_child("LutVal", boost::property_tree::ptree());
        if (useEndcapStubsRInExtr && ((iLayer >= 6 && iLayer <= 9) || (iLayer >= 15 && iLayer <= 17)))
          lutVal.add("<xmlattr>.key", extrFactors.first);
        else
          lutVal.add("<xmlattr>.key", extrFactors.first);

        double value = round(extrapolMultiplier * extrFactors.second / norm);
        lutVal.add("<xmlattr>.value", value);

        edm::LogVerbatim("OMTFReconstruction")
            << std::setw(4) << " key = " << extrFactors.first << " extrFactors.second " << std::setw(10)
            << extrFactors.second << " norm " << std::setw(6) << norm << " value/norm " << std::setw(10)
            << extrFactors.second / norm << " value " << value << std::endl;
      }
    }
  }

  boost::property_tree::write_xml("ExtrapolationFactors.xml",
                                  tree,
                                  std::locale(),
                                  boost::property_tree::xml_parser::xml_writer_make_settings<std::string>(' ', 2));
}

template <class GoldenPatternType>
void OMTFProcessor<GoldenPatternType>::loadExtrapolFactors(const std::string& filename) {
  boost::property_tree::ptree tree;

  boost::property_tree::read_xml(filename, tree);

  edm::LogVerbatim("OMTFReconstruction") << "loadExtrapolFactors from file " << filename << std::endl;

  extrapolMultiplier = tree.get<int>("ExtrapolationFactors.<xmlattr>.multiplier");
  edm::LogVerbatim("OMTFReconstruction") << "extrapolMultiplier " << extrapolMultiplier << std::endl;

  auto& lutNodes = tree.get_child("ExtrapolationFactors");
  for (boost::property_tree::ptree::value_type& lutNode : lutNodes) {
    if (lutNode.first == "Lut") {
      int iRefLayer = lutNode.second.get<int>("<xmlattr>.RefLayer");
      int iLayer = lutNode.second.get<int>("<xmlattr>.Layer");
      std::string keyType = lutNode.second.get<std::string>("<xmlattr>.KeyType");

      LogTrace("OMTFReconstruction") << "iRefLayer " << iRefLayer << " iLayer " << iLayer << " keyType " << keyType
                                     << std::endl;

      auto& valueNodes = lutNode.second;
      for (boost::property_tree::ptree::value_type& valueNode : valueNodes) {
        if (valueNode.first == "LutVal") {
          int key = valueNode.second.get<int>("<xmlattr>.key");
          float value = valueNode.second.get<float>("<xmlattr>.value");
          extrapolFactors.at(iRefLayer).at(iLayer)[key] = value;
          LogTrace("OMTFReconstruction") << "key " << key << " value " << value << std::endl;
        }
      }
    }
  }
}

/////////////////////////////////////////////////////////

template class OMTFProcessor<GoldenPattern>;
template class OMTFProcessor<GoldenPatternWithStat>;
template class OMTFProcessor<GoldenPatternWithThresh>;
