#include "L1Trigger/L1TMuonOverlapPhase1/interface/MuonStubMakerBase.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/ProcConfigurationBase.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/OMTFinputMaker.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/OMTFConfiguration.h"

#include "DataFormats/CSCDigi/interface/CSCCorrelatedLCTDigi.h"
#include "DataFormats/L1DTTrackFinder/interface/L1MuDTChambPhDigi.h"
#include "DataFormats/L1DTTrackFinder/interface/L1MuDTChambThDigi.h"
#include "DataFormats/MuonData/interface/MuonDigiCollection.h"
#include "DataFormats/MuonDetId/interface/CSCDetId.h"
#include "DataFormats/RPCDigi/interface/RPCDigi.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include <boost/property_tree/ptree.hpp>
#include <iostream>
#include <iterator>
#include <utility>

// Helper function to map hwNumber to hwName based on the layer mapping
std::string getHwNameFromHwNumber(unsigned int hwNumber) {
  switch (hwNumber) {
    case 101: return "MB1";
    case 1101: return "MB1b";
    case 102: return "MB2";
    case 1102: return "MB2b";
    case 103: return "MB3";
    case 1103: return "MB3b";
    case 201: return "ME1/3";
    case 202: return "ME2/2";
    case 203: return "ME3/2";
    case 2011: return "ME1/2";
    case 301: return "RB1in";
    case 302: return "RB1out";
    case 303: return "RB2in";
    case 304: return "RB2out";
    case 305: return "RB3";
    case 311: return "RE1/3";
    case 312: return "RE2/3";
    case 313: return "RE3/3";
    default: return "Unknown";
  }
}

unsigned int getLayerBatchSize(unsigned int logicLayer) {
  if (logicLayer >= 10 && logicLayer <= 13) {
    return 4;
  }
  return 2;
}

unsigned int getInputBatch(unsigned int logicLayer, unsigned int inputNumber) {
  return inputNumber % getLayerBatchSize(logicLayer);
}

// Helper function to calculate chamber_wrapped for CSC chambers
// This follows the same logic as OMTFinputMaker::getInputNumber() but returns the chamber position
unsigned int calculateCSCChamberWrapped(const CSCDetId& csc, unsigned int iProcessor, l1t::tftype procTyp, const OMTFConfiguration* omtfConfig) {
  unsigned int aSector = csc.chamber();
  unsigned int aMin = omtfConfig->getEndcap10DegMin()[iProcessor];
  
  // Handle boundary wrapping first (same as in getInputNumber)
  if (iProcessor == (omtfConfig->nProcessors() - 1) && aSector < 5) {
    aSector += 36;  // 36 chambers total in 10-degree system
  }
  
  // Check if this is a 20-degree chamber (station > 1 and ring == 1 for EMTF)
  if ((procTyp == l1t::tftype::emtf_pos || procTyp == l1t::tftype::emtf_neg) && 
      csc.station() > 1 && csc.ring() == 1) {
    aMin = omtfConfig->getEndcap20DegMin()[iProcessor];
    // Different boundary wrapping for 20-degree chambers
    if (iProcessor == (omtfConfig->nProcessors() - 1) && csc.chamber() < 3) {
      aSector = csc.chamber() + 18;  // 18 chambers total in 20-degree system
    } else {
      aSector = csc.chamber();
    }
  }
  
  // This gives the chamber index within this processor's range (0-based)
  unsigned int chamberWrapped = aSector - aMin;
  
  return chamberWrapped;
}

// Helper function to calculate sector_wrapped for RPC chambers
// For Barrel RPC: sectors 1-12 (30-degree), wraps to 0-4 per processor (similar to DT)
// For Endcap RPC: 10-degree sectors (1-36), wraps to 0-12 per processor (similar to CSC)
// Note: The roll parameter is NOT used here - it only affects input number within a sector
// Helper function to calculate sector_wrapped for DT chambers
unsigned int calculateDTSectorWrapped(const DTChamberId& dtId, unsigned int iProcessor, const OMTFConfiguration* omtfConfig) {
  int sector = dtId.sector();
  int aMin = omtfConfig->getBarrelMin()[iProcessor];
  
  // Handle wrap-around for last processor (sectors 1,2 belong to proc 2)
  int aSector = sector;
  if (iProcessor == (omtfConfig->nProcessors() - 1) && aSector < 3) {
    aSector += 12;  // 12 sectors total in barrel
  }
  
  // Calculate sector_wrapped = sector - aMin
  // This gives the correct hardware index 0-4
  return aSector - aMin;
}

// Helper function to calculate chamber_wrapped for endcap RPC chambers
unsigned int calculateRPCEndcapChamberWrapped(const RPCDetId& rpc, unsigned int iProcessor, const OMTFConfiguration* omtfConfig) {
  // For endcap RPC: convert sector/subsector to effective chamber
  // Effective chamber = (sector-1)*6 + subsector
  unsigned int effectiveChamber = (rpc.sector() - 1) * 6 + rpc.subsector();
  unsigned int aMin = omtfConfig->getEndcap10DegMin()[iProcessor];
  
  // Handle boundary wrapping for last processor
  if (iProcessor == (omtfConfig->nProcessors() - 1) && effectiveChamber < 5) {
    effectiveChamber += 36;  // 36 chambers total in 10-degree system
  }
  
  return effectiveChamber - aMin;
}

unsigned int calculateRPCSectorWrapped(const RPCDetId& rpc, unsigned int iProcessor, const OMTFConfiguration* omtfConfig) {
  unsigned int aSector = 0;
  unsigned int sectorWrapped = 0;
  
  if (rpc.region() == 0) {
    // Barrel RPC: uses 30-degree sectors (1-12), same as DT
    // Configuration shows:
    // Proc 0: barrelMin=1, barrelMax=5 → sectors {1,2,3,4,5}
    // Proc 1: barrelMin=5, barrelMax=9 → sectors {5,6,7,8,9}
    // Proc 2: barrelMin=9, barrelMax=1 → sectors {9,10,11,12,1}
    // Sectors 1, 5, and 9 are shared between adjacent processors
    // Hardware expects indices 0-4 for each processor
    
    aSector = rpc.sector();
    unsigned int aMin = omtfConfig->getBarrelMin()[iProcessor];
    
    // Handle wrap-around for last processor (sectors 1,2 belong to proc 2)
    if (iProcessor == (omtfConfig->nProcessors() - 1) && aSector < 3) {
      aSector += 12;  // 12 sectors total in barrel
    }
    
    // Calculate sector_wrapped = sector - aMin
    // This gives the correct hardware index 0-4
    sectorWrapped = aSector - aMin;
  } else {
    // Endcap RPC: uses 10-degree sectors (1-36)
    // Convert sector and subsector to 10-degree sector number
    aSector = (rpc.sector() - 1) * 6 + rpc.subsector();
    unsigned int aMin = omtfConfig->getEndcap10DegMin()[iProcessor];
    
    // Handle boundary wrapping for last processor
    if (iProcessor == (omtfConfig->nProcessors() - 1) && aSector < 5) {
      aSector += 36;  // 36 sectors total in endcap (10-degree)
    }
    
    // This gives the sector index within this processor's range (0-based)
    sectorWrapped = aSector - aMin;
  }
  
  return sectorWrapped;
}

// Helper function to calculate the logic region based on phi value
// Uses the phi ranges from the configuration to determine the correct region (0-11)
// Returns -1 if no matching RefHitDef is found (since 0 is a valid region)
int calculateLogicRegion(int phiHw, unsigned int iRefLayer, unsigned int iInput, const OMTFConfiguration* omtfConfig) {
  if (!omtfConfig) {
    return -1;
  }
  
  const auto& refHitDefs = omtfConfig->getRefHitsDefs();
  
  // The refHitDefs is indexed by [processor][refHit], need to check processor 0
  if (refHitDefs.empty()) {
    return -1;
  }
  
  // Use processor 0 (positive endcap) configuration
  const auto& processor0RefHits = refHitDefs[0];
  
  // Loop through all reference hits for processor 0 to find the exact match
  // Must match BOTH iRefLayer AND iInput to get the correct region
  for (const auto& refHitDef : processor0RefHits) {
    if (refHitDef.iRefLayer == iRefLayer && refHitDef.iInput == iInput) {
      // Found the matching RefHitDef, verify phi is in range
      if (refHitDef.fitsRange(phiHw)) {
        return refHitDef.iRegion;
      }
    }
  }
  
  // No match found - return -1 since region 0 is a valid region value
  return -1;
}

void MuonStubMakerBase::addGlobalReferenceStub(const std::string& detectorType, unsigned int processor, unsigned int refLayerNumber, 
                                               unsigned int logicLayer, int phiHw, int phiBHw, int etaHw, unsigned int qualityHw, 
                                               unsigned int detId, const std::string& hwName, int endcap, 
                                               unsigned int station, 
                                               int cscRing, int cscChamber, int cscChamberWrapped,
                                               int dtSector, int dtSectorWrapped,
                                               int logicRegion) {
  // Create a reference stub element in the static global tree
  boost::property_tree::ptree stub;
  stub.add("<xmlattr>.processor", processor);
  stub.add("<xmlattr>.refLayerNumber", refLayerNumber);
  stub.add("<xmlattr>.logicLayer", logicLayer);
  stub.add("<xmlattr>.phiHw", phiHw);
  stub.add("<xmlattr>.phiBHw", phiBHw);
  stub.add("<xmlattr>.etaHw", etaHw);
  stub.add("<xmlattr>.qualityHw", qualityHw);
  stub.add("<xmlattr>.detId", detId);
  stub.add("<xmlattr>.hwName", hwName);
  stub.add("<xmlattr>.endcap", endcap);
  stub.add("<xmlattr>.station", station);
  // CSC-specific fields
  stub.add("<xmlattr>.ring", cscRing);
  stub.add("<xmlattr>.chamber", cscChamber);
  stub.add("<xmlattr>.chamber_wrapped", cscChamberWrapped);
  // DT-specific fields
  stub.add("<xmlattr>.sector", dtSector);
  stub.add("<xmlattr>.sector_wrapped", dtSectorWrapped);
  stub.add("<xmlattr>.logicRegion", logicRegion);
  
  globalReferenceStubsTreeStatic.add_child("ReferenceStub", stub);
}

void MuonStubMakerBase::flushReferenceStubs(std::vector<std::unique_ptr<IOMTFEmulationObserver> >& observers) {
  for (auto& observer : observers) {
    observer->addProcesorData("ReferenceStubs", globalReferenceStubsTreeStatic);
  }
}

/////////////////////////////////////
void DtDigiToStubsConverter::loadDigis(const edm::Event& event) {
  event.getByToken(inputTokenDtPh, dtPhDigis);
  event.getByToken(inputTokenDtTh, dtThDigis);
}

void DtDigiToStubsConverter::makeStubs(MuonStubPtrs2D& muonStubsInLayers,
                                       unsigned int iProcessor,
                                       l1t::tftype procTyp,
                                       int bxFrom,
                                       int bxTo,
                                       std::vector<std::unique_ptr<IOMTFEmulationObserver> >& observers,
                                       XmlIOCache& xmlCache) {
  boost::property_tree::ptree procDataTree;

  for (const auto& digiIt : *dtPhDigis->getContainer()) {
    DTChamberId detid(digiIt.whNum(), digiIt.stNum(), digiIt.scNum() + 1);

    ///Check it the data fits into given processor input range
    if (!acceptDigi(detid, iProcessor, procTyp))
      continue;

    if (digiIt.bxNum() >= bxFrom && digiIt.bxNum() <= bxTo) {
      addDTphiDigi(muonStubsInLayers, digiIt, dtThDigis.product(), iProcessor, procTyp);
    }
  }

  if (!mergePhiAndTheta) {
    for (auto& thetaDigi : (*(dtThDigis->getContainer()))) {
      if (thetaDigi.bxNum() >= bxFrom && thetaDigi.bxNum() <= bxTo) {
        addDTetaStubs(muonStubsInLayers, thetaDigi, iProcessor, procTyp);
      }
    }
  }

  // Notify observers - DT XML generation happens in InputMakerPhase2, not here
  // Note: Phase-1 DT does not use XmlIOCache as Phase-2 handles DT stubs
  for (auto& observer : observers) {
    observer->addProcesorData("DTdigis", procDataTree);
  }
  //LogTrace("l1tOmtfEventPrint")<<__FUNCTION__<<":"<<__LINE__<<" iProcessor "<<iProcessor<<std::endl;
}
///////////////////////////////////////
///////////////////////////////////////

void CscDigiToStubsConverter::makeStubs(MuonStubPtrs2D& muonStubsInLayers,
                                        unsigned int iProcessor,
                                        l1t::tftype procTyp,
                                        int bxFrom,
                                        int bxTo,
                                        std::vector<std::unique_ptr<IOMTFEmulationObserver> >& observers,
                                        XmlIOCache& xmlCache) {
  boost::property_tree::ptree cscDigisTree;
  boost::property_tree::ptree cscStubsTree;
  // Use global reference stubs tree instead of local one
  
  // Track order of digis per chamber for ordering information
  std::map<unsigned int, unsigned int> chamberDigiOrder;
  
  // Track stub_order per (chamber_wrapped + logicLayer) combination
  // Key format: "chamber_X_layer_Y" where X is chamber_wrapped and Y is logicLayer
  std::map<std::string, int> cscStubOrder;

  auto chamber = cscDigis->begin();
  auto chend = cscDigis->end();
  for (; chamber != chend; ++chamber) {
    unsigned int rawid = (*chamber).first;
    ///Check it the data fits into given processor input range
    CSCDetId csc(rawid);
    if (!acceptDigi(csc, iProcessor, procTyp))
      continue;

    // Reset order counter for each chamber
    chamberDigiOrder[rawid] = 0;

    auto digi = (*chamber).second.first;
    auto dend = (*chamber).second.second;
    for (; digi != dend; ++digi) {
      ///Check if LCT trigger primitive has the right BX.
      int digiBx = digi->getBX() - config->cscLctCentralBx();

      if (digiBx >= bxFrom && digiBx <= bxTo) {
        addCSCstubs(muonStubsInLayers, rawid, *digi, iProcessor, procTyp);

        // === ADD XML DATA COLLECTION FOR CSV EXPORT ===
        auto& cscDigi = cscDigisTree.add_child("cscDigi", boost::property_tree::ptree());
        // Detector ID fields
        cscDigi.add("<xmlattr>.detId", rawid);
        cscDigi.add("<xmlattr>.endcap", csc.endcap());
        cscDigi.add("<xmlattr>.station", csc.station());
        cscDigi.add("<xmlattr>.ring", csc.ring());
        cscDigi.add("<xmlattr>.chamber", csc.chamber());
        cscDigi.add("<xmlattr>.layer", csc.layer());
        
        // Add ordering information within chamber
        cscDigi.add("<xmlattr>.chamberOrder", chamberDigiOrder[rawid]++);
        
        // Core LCT data fields
        cscDigi.add("<xmlattr>.trknmb", digi->getTrknmb());
        cscDigi.add("<xmlattr>.valid", digi->isValid());
        cscDigi.add("<xmlattr>.quality", digi->getQuality());
        cscDigi.add("<xmlattr>.keywire", digi->getKeyWG());
        cscDigi.add("<xmlattr>.strip", digi->getStrip());
        cscDigi.add("<xmlattr>.pattern", digi->getPattern());
        cscDigi.add("<xmlattr>.bend", digi->getBend());
        cscDigi.add("<xmlattr>.bx", digi->getBX());
        cscDigi.add("<xmlattr>.mpclink", digi->getMPCLink());
        cscDigi.add("<xmlattr>.bx0", digi->getBX0());
        cscDigi.add("<xmlattr>.syncErr", digi->getSyncErr());
        cscDigi.add("<xmlattr>.cscID", digi->getCSCID());
        
        // Run-3 specific fields
        if (digi->isRun3()) {
          cscDigi.add("<xmlattr>.isRun3", true);
          cscDigi.add("<xmlattr>.quartStripBit", digi->getQuartStripBit());
          cscDigi.add("<xmlattr>.eighthStripBit", digi->getEighthStripBit());
          cscDigi.add("<xmlattr>.run3Pattern", digi->getRun3Pattern());
          cscDigi.add("<xmlattr>.slope", digi->getSlope());
          cscDigi.add("<xmlattr>.hmt", digi->getHMT());
        } else {
          cscDigi.add("<xmlattr>.isRun3", false);
        }
        
        // Additional computed fields
        cscDigi.add("<xmlattr>.fractionalStrip", digi->getFractionalStrip());
        cscDigi.add("<xmlattr>.fractionalSlope", digi->getFractionalSlope());
        cscDigi.add("<xmlattr>.clctPattern", digi->getCLCTPattern());
        cscDigi.add("<xmlattr>.stripType", digi->getStripType());
        cscDigi.add("<xmlattr>.bxData", digi->getBXData());
        //cscDigi.add("<xmlattr>.type", digi->getType());
        
        // CSC angle conversion parameters
        CscConversionInfo convInfo = getCscConversionInfo(rawid, *digi, iProcessor, procTyp);
        cscDigi.add("<xmlattr>.offset", convInfo.offset);
        cscDigi.add("<xmlattr>.scale", convInfo.scale);
        cscDigi.add("<xmlattr>.order", convInfo.order);
        cscDigi.add("<xmlattr>.halfStrip", convInfo.halfStrip);
        
        // ADD hwName attribute for CSC digi
        const OMTFConfiguration* omtfConfigHw = dynamic_cast<const OMTFConfiguration*>(config);
        if (omtfConfigHw) {
          unsigned int hwNumber = omtfConfigHw->getLayerNumber(rawid);
          if (omtfConfigHw->getHwToLogicLayer().find(hwNumber) != omtfConfigHw->getHwToLogicLayer().end()) {
            std::string hwName = getHwNameFromHwNumber(hwNumber);
            cscDigi.add("<xmlattr>.hwName", hwName);
          }
          
          // ADD chamber_wrapped field using helper function
          unsigned int chamberWrapped = calculateCSCChamberWrapped(csc, iProcessor, procTyp, omtfConfigHw);
          cscDigi.add("<xmlattr>.chamber_wrapped", chamberWrapped);
        }
      }
    }
    
    // === ADD GOLDEN STUB DATA (ONCE PER CHAMBER, OUTSIDE DIGI LOOP) ===
    // Check if CSC stubs were added to muonStubsInLayers for this chamber
    // NOTE: We need to search through all inputs for this layer, not just calculate one inputNumber
    // from rawid, because multiple digis from the same chamber can map to different inputNumbers
    const OMTFConfiguration* omtfConfig = dynamic_cast<const OMTFConfiguration*>(config);
    if (omtfConfig) {
      unsigned int hwNumber = omtfConfig->getLayerNumber(rawid);
      if (omtfConfig->getHwToLogicLayer().find(hwNumber) != omtfConfig->getHwToLogicLayer().end()) {
        unsigned int iLayer = omtfConfig->getHwToLogicLayer().at(hwNumber);
        if (iLayer < muonStubsInLayers.size()) {
          // Search through all inputs in this layer to find stubs from this chamber (rawid)
          for (unsigned int iInput = 0; iInput < muonStubsInLayers[iLayer].size(); ++iInput) {
          if (muonStubsInLayers[iLayer][iInput] && muonStubsInLayers[iLayer][iInput]->detId == static_cast<int>(rawid)) {
            auto& stub = muonStubsInLayers[iLayer][iInput];
                
                // Add golden stub data to separate XML tree
                auto& cscStub = cscStubsTree.add_child("CSCstub", boost::property_tree::ptree());
                //cscStub.add("<xmlattr>.type", static_cast<int>(stub->type));
                cscStub.add("<xmlattr>.logicLayer", stub->logicLayer);
                cscStub.add("<xmlattr>.inputNumber", iInput);
                cscStub.add("<xmlattr>.inputBatch", getInputBatch(stub->logicLayer, iInput));
                cscStub.add("<xmlattr>.phiHw", stub->phiHw);
                cscStub.add("<xmlattr>.etaHw", stub->etaHw);
                cscStub.add("<xmlattr>.qualityHw", stub->qualityHw);
                cscStub.add("<xmlattr>.phiBHw", stub->phiBHw);
                cscStub.add("<xmlattr>.bx", stub->bx);
                cscStub.add("<xmlattr>.timing", stub->timing);
                cscStub.add("<xmlattr>.r", stub->r);
                cscStub.add("<xmlattr>.detId", stub->detId);
                
                // Add CSC chamber identification fields
                CSCDetId cscId(stub->detId);
                cscStub.add("<xmlattr>.endcap", cscId.endcap());
                cscStub.add("<xmlattr>.station", cscId.station());
                cscStub.add("<xmlattr>.ring", cscId.ring());
                cscStub.add("<xmlattr>.chamber", cscId.chamber());
                cscStub.add("<xmlattr>.layer", cscId.layer());
                
                // Find hwNumber from logic layer and get hwName
                unsigned int stubHwNumber = omtfConfig->getLogicToHwLayer().at(stub->logicLayer);
                std::string hwName = getHwNameFromHwNumber(stubHwNumber);
                cscStub.add("<xmlattr>.hwName", hwName);
                
                // Add chamber_wrapped for CSC hardware compatibility
                unsigned int chamberWrapped = calculateCSCChamberWrapped(cscId, iProcessor, procTyp, omtfConfig);
                cscStub.add("<xmlattr>.chamber_wrapped", chamberWrapped);
                
                // Add stub_order based on (chamber_wrapped + logicLayer) combination
                std::string stubKey = "chamber_" + std::to_string(chamberWrapped) + "_layer_" + std::to_string(stub->logicLayer);
                int stubOrder = cscStubOrder[stubKey]++;
                cscStub.add("<xmlattr>.stub_order", stubOrder);
                
                // Check if this is a reference layer stub and add to reference collection
                const auto& refToLogicNumbers = omtfConfig->getRefToLogicNumber();
                for (unsigned int iRefLayer = 0; iRefLayer < refToLogicNumbers.size(); ++iRefLayer) {
                  if (refToLogicNumbers[iRefLayer] == (int)stub->logicLayer) {
                    // This is a reference layer stub - add to reference collection
                    unsigned int chamberWrapped = calculateCSCChamberWrapped(cscId, iProcessor, procTyp, omtfConfig);
                    unsigned int logicRegion = calculateLogicRegion(stub->phiHw, iRefLayer, iInput, omtfConfig);
                    
                    MuonStubMakerBase::addGlobalReferenceStub("CSC", iProcessor, iRefLayer, stub->logicLayer, stub->phiHw, stub->phiBHw, stub->etaHw, 
                                                             stub->qualityHw, stub->detId, hwName, cscId.endcap(), cscId.station(), 
                                                             cscId.ring(), cscId.chamber(), chamberWrapped,
                                                             -1, -1,  // DT fields: not applicable for CSC
                                                             logicRegion);
                    break; // Found the reference layer, no need to continue
                  }
                }
              }
              }  // End of for loop through all inputs
            }
          }
        }
      }  // End chamber loop (for (; chamber != chend; ++chamber))

  // === ADD DATA TO XMLIOCACHE ===
  // Add CSC digis to cache
  for (const auto& digiNode : cscDigisTree) {
    xmlCache.addDigi(iProcessor, "CSC", digiNode.second);
  }

  // Add CSC stubs to cache
  for (const auto& stubNode : cscStubsTree) {
    const auto& stubAttrs = stubNode.second;
    omtf::StubRecord srec;
    srec.type = "CSC";

    // Extract key fields from attributes
    unsigned int detId = stubAttrs.get<unsigned int>("<xmlattr>.detId");
    int logicLayer = stubAttrs.get<int>("<xmlattr>.logicLayer");
    int inputNumber = stubAttrs.get<int>("<xmlattr>.inputNumber");
    int bx = stubAttrs.get<int>("<xmlattr>.bx");

    srec.key = {detId, logicLayer, inputNumber, bx};
    srec.attrs = stubAttrs;

    xmlCache.addStub(iProcessor, srec);
    // Note: References will be marked by OMTFProcessor, not here
  }

  // Old XML sections removed - now using unified XML output via XmlIOCache
}

void RpcDigiToStubsConverter::makeStubs(MuonStubPtrs2D& muonStubsInLayers,
                                        unsigned int iProcessor,
                                        l1t::tftype procTyp,
                                        int bxFrom,
                                        int bxTo,
                                        std::vector<std::unique_ptr<IOMTFEmulationObserver> >& observers,
                                        XmlIOCache& xmlCache) {
  //LogTrace("l1tOmtfEventPrint") << __FUNCTION__ << ":" << __LINE__ <<" RPC HITS, processor : " << iProcessor<<" "<<std::endl;

  boost::property_tree::ptree rpcDigisTree;
  boost::property_tree::ptree rpcStubsTree;
  // Use global reference stubs tree instead of local one
  
  // Track order of digis per roll for ordering information
  std::map<unsigned int, unsigned int> rollDigiOrder;
  
  // Track stub_order per (sector_wrapped/chamber_wrapped + logicLayer) combination
  // Key format: "sector_X_layer_Y" or "chamber_X_layer_Y"
  std::map<std::string, int> rpcStubOrder;

  const RPCDigiCollection& rpcDigiCollection = *rpcDigis;
  for (auto rollDigis : rpcDigiCollection) {
    RPCDetId roll = rollDigis.first;

    //debug
    //if(roll.region() != 0  &&  abs(roll.station()) >= 3 && roll.ring() == 1 )
    /*    {
      //iRPC
      for (auto pDigi=rollDigis.second.first; pDigi != rollDigis.second.second; pDigi++) {
        LogTrace("l1tOmtfEventPrint")<<__FUNCTION__<<":"<<__LINE__<<" roll "<<roll
            <<" strip "<<pDigi->strip()
            <<" hasX "<<pDigi->hasX()<<" coordinateX "<<pDigi->coordinateX()<<" hasY "<<pDigi->hasY()<<" coordinateY "<<pDigi->coordinateY()
            <<" bx "<<pDigi->bx()<<" time "<<pDigi->time()<<" irpc"<<std::endl;
      }
      //continue;
    }*/

    //LogTrace("l1tOmtfEventPrint") << __FUNCTION__ << ":" << __LINE__ <<" roll "<<roll<<" "<<std::endl;

    if (!acceptDigi(roll, iProcessor, procTyp))
      continue;

    // Reset order counter for each roll
    rollDigiOrder[roll.rawId()] = 0;

    ///To find the clusters we have to copy the digis in chamber to sort them (not optimal).
    //  for (auto tdigi = rollDigis.second.first; tdigi != rollDigis.second.second; tdigi++) { std::cout << "RPC DIGIS: " << roll.rawId()<< " "<<roll<<" digi: " << tdigi->strip() <<" bx: " << tdigi->bx() << std::endl; }
    std::vector<RPCDigi> digisCopy;

    for (auto pDigi = rollDigis.second.first; pDigi != rollDigis.second.second; pDigi++) {
      if (pDigi->bx() >= bxFrom && pDigi->bx() <= bxTo) {
        digisCopy.push_back(*pDigi);

        // === ADD XML DATA COLLECTION FOR CSV EXPORT ===
        auto& rpcDigi = rpcDigisTree.add_child("rpcDigi", boost::property_tree::ptree());
        // Detector ID fields
        rpcDigi.add("<xmlattr>.rpcID", roll.rawId());
        rpcDigi.add("<xmlattr>.region", roll.region());
        rpcDigi.add("<xmlattr>.ring", roll.ring());
        rpcDigi.add("<xmlattr>.station", roll.station());
        rpcDigi.add("<xmlattr>.sector", roll.sector());
        rpcDigi.add("<xmlattr>.layer", roll.layer());
        rpcDigi.add("<xmlattr>.subsector", roll.subsector());
        rpcDigi.add("<xmlattr>.roll", roll.roll());
        
        // Add ordering information within roll
        rpcDigi.add("<xmlattr>.rollOrder", rollDigiOrder[roll.rawId()]++);
        
        // Core RPC digi fields
        rpcDigi.add("<xmlattr>.strip", pDigi->strip());
        rpcDigi.add("<xmlattr>.bx", pDigi->bx());
        
        // Timing and spatial information
        rpcDigi.add("<xmlattr>.time", pDigi->time());
        rpcDigi.add("<xmlattr>.coordinateX", pDigi->coordinateX());
        rpcDigi.add("<xmlattr>.coordinateY", pDigi->coordinateY());
        rpcDigi.add("<xmlattr>.deltaTime", pDigi->deltaTime());
        rpcDigi.add("<xmlattr>.deltaX", pDigi->deltaX());
        rpcDigi.add("<xmlattr>.deltaY", pDigi->deltaY());
        
        // Flags for available information
        rpcDigi.add("<xmlattr>.hasTime", pDigi->hasTime());
        rpcDigi.add("<xmlattr>.hasX", pDigi->hasX());
        rpcDigi.add("<xmlattr>.hasY", pDigi->hasY());
        rpcDigi.add("<xmlattr>.isPseudoDigi", pDigi->isPseudoDigi());
      }
    }

    std::vector<RpcCluster> clusters = rpcClusterization->getClusters(roll, digisCopy);

    for (auto& cluster : clusters) {
      addRPCstub(muonStubsInLayers, roll, cluster, iProcessor, procTyp);
    }
    
    // === ADD GOLDEN STUB DATA FOR RPC (ONCE PER ROLL, OUTSIDE CLUSTER LOOP) ===
    // Check if RPC stubs were added to muonStubsInLayers for this roll
    // NOTE: We need to search through all inputs for this layer, not just calculate one inputNumber
    // from roll.rawId(), because multiple clusters from the same roll can map to different inputNumbers
    const OMTFConfiguration* omtfConfig = dynamic_cast<const OMTFConfiguration*>(config);
      if (omtfConfig) {
        unsigned int hwNumber = omtfConfig->getLayerNumber(roll.rawId());
        if (omtfConfig->getHwToLogicLayer().find(hwNumber) != omtfConfig->getHwToLogicLayer().end()) {
          unsigned int iLayer = omtfConfig->getHwToLogicLayer().at(hwNumber);
          if (iLayer < muonStubsInLayers.size()) {
            // Search through all inputs in this layer to find stubs from this roll (rawId)
            for (unsigned int iInput = 0; iInput < muonStubsInLayers[iLayer].size(); ++iInput) {
            if (muonStubsInLayers[iLayer][iInput] && muonStubsInLayers[iLayer][iInput]->detId == static_cast<int>(roll.rawId())) {
              auto& stub = muonStubsInLayers[iLayer][iInput];

              if (stub->type == MuonStub::RPC_DROPPED) {
                continue;
              }
              
              // Determine if this is barrel (region=0) or endcap (region!=0)
              RPCDetId rpcId(roll.rawId());
              bool isBarrel = (rpcId.region() == 0);
              
              // Add golden stub data to separate XML tree (RPCbStub for barrel, RPCeStub for endcap)
              std::string stubNodeName = isBarrel ? "RPCbStub" : "RPCeStub";
              auto& rpcStub = rpcStubsTree.add_child(stubNodeName, boost::property_tree::ptree());
              // Add a textual type attribute indicating barrel/endcap RPC
              // Determine RPC type from logicLayer: <10 => RPCb, >=10 => RPCe
              rpcStub.add("<xmlattr>.logicLayer", stub->logicLayer);
              rpcStub.add("<xmlattr>.inputNumber", iInput);
              rpcStub.add("<xmlattr>.inputBatch", getInputBatch(stub->logicLayer, iInput));
              rpcStub.add("<xmlattr>.phiHw", stub->phiHw);
              rpcStub.add("<xmlattr>.etaHw", stub->etaHw);
              rpcStub.add("<xmlattr>.qualityHw", stub->qualityHw);
              rpcStub.add("<xmlattr>.phiBHw", stub->phiBHw);
              rpcStub.add("<xmlattr>.bx", stub->bx);
              rpcStub.add("<xmlattr>.timing", stub->timing);
              rpcStub.add("<xmlattr>.r", stub->r);
              rpcStub.add("<xmlattr>.detId", stub->detId);
              
              // Add RPC detector identification fields
              rpcStub.add("<xmlattr>.region", rpcId.region());
              rpcStub.add("<xmlattr>.station", rpcId.station());
              rpcStub.add("<xmlattr>.ring", rpcId.ring());
              rpcStub.add("<xmlattr>.sector", rpcId.sector());
              rpcStub.add("<xmlattr>.layer", rpcId.layer());
              rpcStub.add("<xmlattr>.subsector", rpcId.subsector());
              rpcStub.add("<xmlattr>.roll", rpcId.roll());
              
              // Find hwNumber from logic layer and get hwName
              unsigned int stubHwNumber = omtfConfig->getLogicToHwLayer().at(stub->logicLayer);
              std::string hwName = getHwNameFromHwNumber(stubHwNumber);
              rpcStub.add("<xmlattr>.hwName", hwName);
              
              // Add wrapped field based on barrel/endcap
              std::string stubKey;
              if (isBarrel) {
                // RPCb: Calculate sector_wrapped (0-4, like DT)
                unsigned int sectorWrapped = calculateRPCSectorWrapped(rpcId, iProcessor, omtfConfig);
                rpcStub.add("<xmlattr>.sector_wrapped", sectorWrapped);
                rpcStub.add("<xmlattr>.chamber_wrapped", -1);
                stubKey = "sector_" + std::to_string(sectorWrapped) + "_layer_" + std::to_string(stub->logicLayer);
              } else {
                // RPCe: Calculate chamber_wrapped (0-12, like CSC)
                // For endcap RPC: effective chamber = (sector-1)*6 + subsector
                unsigned int effectiveChamber = (rpcId.sector() - 1) * 6 + rpcId.subsector();
                unsigned int aMin = omtfConfig->getEndcap10DegMin()[iProcessor];
                
                // Handle boundary wrapping for last processor
                if (iProcessor == (omtfConfig->nProcessors() - 1) && effectiveChamber < 5) {
                  effectiveChamber += 36;  // 36 chambers total in 10-degree system
                }
                
                unsigned int chamberWrapped = effectiveChamber - aMin;
                rpcStub.add("<xmlattr>.sector_wrapped", -1);
                rpcStub.add("<xmlattr>.chamber_wrapped", chamberWrapped);
                stubKey = "chamber_" + std::to_string(chamberWrapped) + "_layer_" + std::to_string(stub->logicLayer);
              }
              
              // Add stub_order based on (sector_wrapped/chamber_wrapped + logicLayer) combination
              int stubOrder = rpcStubOrder[stubKey]++;
              rpcStub.add("<xmlattr>.stub_order", stubOrder);
              
              // Check if this is a reference layer stub and add to reference collection
              const auto& refToLogicNumbers = omtfConfig->getRefToLogicNumber();
              for (unsigned int iRefLayer = 0; iRefLayer < refToLogicNumbers.size(); ++iRefLayer) {
                if (refToLogicNumbers[iRefLayer] == (int)stub->logicLayer) {
                  // This is a reference layer stub - add to reference collection
                  RPCDetId rpcId(roll.rawId());
                  unsigned int logicRegion = calculateLogicRegion(stub->phiHw, iRefLayer, iInput, omtfConfig);
                  
                  // RPC parameters depend on barrel vs endcap
                  // Initialize to -1 (not applicable) before setting detector-specific values
                  int cscRing = -1;
                  int cscChamber = -1;
                  int cscChamberWrapped = -1;
                  int dtSector = -1;
                  int dtSectorWrapped = -1;
                  
                  if (isBarrel) {
                    // RPC Barrel: Use sector and sector_wrapped (like DT)
                    dtSector = rpcId.sector();
                    dtSectorWrapped = calculateRPCSectorWrapped(rpcId, iProcessor, omtfConfig);
                    // CSC fields not applicable for barrel
                    cscRing = -1;
                    cscChamber = -1;
                    cscChamberWrapped = -1;
                  } else {
                    // RPC Endcap: Use chamber and chamber_wrapped (like CSC)
                    // Effective chamber = (sector-1)*6 + subsector
                    unsigned int effectiveChamber = (rpcId.sector() - 1) * 6 + rpcId.subsector();
                    unsigned int aMin = omtfConfig->getEndcap10DegMin()[iProcessor];
                    
                    // Handle boundary wrapping for last processor
                    if (iProcessor == (omtfConfig->nProcessors() - 1) && effectiveChamber < 5) {
                      effectiveChamber += 36;  // 36 chambers total in 10-degree system
                    }
                    
                    cscRing = rpcId.ring();
                    cscChamber = effectiveChamber;
                    cscChamberWrapped = effectiveChamber - aMin;
                    // DT fields not applicable for endcap
                    dtSector = -1;
                    dtSectorWrapped = -1;
                  }
                  
                  MuonStubMakerBase::addGlobalReferenceStub("RPC", iProcessor, iRefLayer, stub->logicLayer, stub->phiHw, stub->phiBHw, stub->etaHw, 
                                                           stub->qualityHw, stub->detId, hwName, rpcId.region(), rpcId.station(), 
                                                           cscRing, cscChamber, cscChamberWrapped,  // CSC fields: for RPCe only
                                                           dtSector, dtSectorWrapped,  // DT fields: for RPCb only
                                                           logicRegion);
                  break; // Found the reference layer, no need to continue
                }
              }
            }
            }  // End of for loop through all inputs
          }
        }
      }
  }  // End roll loop (for (auto rollDigis : rpcDigiCollection))

  //removing the RPC stubs that were mark as dropped in the RpcDigiToStubsConverterOmtf::addRPCstub
  //10 is the first RPC layer
  for (unsigned int iLayer = 10; iLayer < muonStubsInLayers.size(); iLayer++) {
    for (unsigned int iInput = 0; iInput < muonStubsInLayers[iLayer].size(); iInput++) {
      if (muonStubsInLayers[iLayer][iInput] && muonStubsInLayers[iLayer][iInput]->type == MuonStub::RPC_DROPPED) {
        LogTrace("l1tOmtfEventPrint") << "RpcDigiToStubsConverter::makeStubs "
                                      << " iProcessor " << iProcessor << " procTyp " << procTyp
                                      << " dropping a stub iLayer " << iLayer << " iInput "
                                      << *(muonStubsInLayers[iLayer][iInput]) << std::endl;
        muonStubsInLayers[iLayer][iInput].reset();
      }
    }
  }

  // === ADD DATA TO XMLIOCACHE ===
  // Add RPC digis to cache
  if (dumpRPCDigis) {
    for (const auto& digiNode : rpcDigisTree) {
      xmlCache.addDigi(iProcessor, "RPC", digiNode.second);
    }
  }

  // Add RPC stubs to cache
  for (const auto& stubNode : rpcStubsTree) {
    const auto& stubAttrs = stubNode.second;
    omtf::StubRecord srec;
  
    //srec.type = "RPC";

    // Extract key fields from attributes
    unsigned int detId = stubAttrs.get<unsigned int>("<xmlattr>.detId");
    int logicLayer = stubAttrs.get<int>("<xmlattr>.logicLayer");
    if (logicLayer < 15) {
      srec.type = "RPCb";  // Barrel RPC
    } else {
      srec.type = "RPCe";  // Endcap RPC
    }

    int inputNumber = stubAttrs.get<int>("<xmlattr>.inputNumber");
    int bx = stubAttrs.get<int>("<xmlattr>.bx");

    srec.key = {detId, logicLayer, inputNumber, bx};
    srec.attrs = stubAttrs;

    xmlCache.addStub(iProcessor, srec);
    // Note: References will be marked by OMTFProcessor, not here
  }

  // Old XML sections removed - now using unified XML output via XmlIOCache
}

///////////////////////////////////////
///////////////////////////////////////
MuonStubMakerBase::MuonStubMakerBase(const ProcConfigurationBase* procConf) : config(procConf), rpcClusterization() {}

///////////////////////////////////////
///////////////////////////////////////
void MuonStubMakerBase::initialize(const edm::ParameterSet& edmCfg,
                                   const edm::EventSetup& es,
                                   const MuonGeometryTokens& muonGeometryTokens) {
  rpcClusterization.configure(
      config->getRpcMaxClusterSize(), config->getRpcMaxClusterCnt(), config->getRpcDropAllClustersIfMoreThanMax());
  
  // Initialize RPC digi export control parameter
  dumpRPCDigis = edmCfg.getParameter<bool>("dumpRPCDigis");
}
///////////////////////////////////////
///////////////////////////////////////
MuonStubMakerBase::~MuonStubMakerBase() {}
///////////////////////////////////////
///////////////////////////////////////

void MuonStubMakerBase::loadAndFilterDigis(const edm::Event& event) {
  for (auto& digiToStubsConverter : digiToStubsConverters)
    digiToStubsConverter->loadDigis(event);
}

void MuonStubMakerBase::buildInputForProcessor(MuonStubPtrs2D& muonStubsInLayers,
                                               unsigned int iProcessor,
                                               l1t::tftype procTyp,
                                               int bxFrom,
                                               int bxTo,
                                               std::vector<std::unique_ptr<IOMTFEmulationObserver> >& observers,
                                               XmlIOCache& xmlCache) {
  //LogTrace("l1tOmtfEventPrint") << __FUNCTION__ << ":" << __LINE__ << " iProcessor " << iProcessor << " preocType "
  //                              << procTyp << std::endl;

  // Clear global reference stubs before processing
  clearGlobalReferenceStubs();

  for (auto& digiToStubsConverter : digiToStubsConverters)
    digiToStubsConverter->makeStubs(muonStubsInLayers, iProcessor, procTyp, bxFrom, bxTo, observers, xmlCache);
  
  // Output all collected reference stubs in one consolidated section
  flushReferenceStubs(observers);
}

// Static member definition
boost::property_tree::ptree MuonStubMakerBase::globalReferenceStubsTreeStatic;

void MuonStubMakerBase::clearGlobalReferenceStubs() {
  globalReferenceStubsTreeStatic.clear();
}

const boost::property_tree::ptree& MuonStubMakerBase::getGlobalReferenceStubs() {
  return globalReferenceStubsTreeStatic;
}
