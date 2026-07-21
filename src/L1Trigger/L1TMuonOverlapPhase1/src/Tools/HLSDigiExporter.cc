#include "L1Trigger/L1TMuonOverlapPhase1/interface/Tools/HLSDigiExporter.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/OMTFinputMaker.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <iomanip>
#include <sstream>

HLSDigiExporter::HLSDigiExporter(const std::string& outputDir)
    : outputDir_(outputDir), currentEvent_(0), currentRun_(0), currentProcessor_(0), 
      currentMtfType_(l1t::omtf_pos), filesOpened_(false) {
  createOutputDirectory();
  edm::LogInfo("HLSDigiExporter") << "HLS Digi Exporter initialized. Output dir: " << outputDir_;
}

HLSDigiExporter::~HLSDigiExporter() {
  if (dtPhiDigiFile_.is_open()) dtPhiDigiFile_.close();
  if (dtThetaDigiFile_.is_open()) dtThetaDigiFile_.close();
  if (cscDigiFile_.is_open()) cscDigiFile_.close();
  if (rpcDigiFile_.is_open()) rpcDigiFile_.close();
  if (goldenResultsFile_.is_open()) goldenResultsFile_.close();
  if (refHitsFile_.is_open()) refHitsFile_.close();
  if (gpResultsFile_.is_open()) gpResultsFile_.close();
  if (gpFinalResultsFile_.is_open()) gpFinalResultsFile_.close();
  if (sortedCandidatesFile_.is_open()) sortedCandidatesFile_.close();
  if (stubsFile_.is_open()) stubsFile_.close();
}

void HLSDigiExporter::observeEventBegin(const edm::Event& iEvent) {
  currentEvent_ = iEvent.id().event();
  currentRun_ = iEvent.id().run();
  
  if (!filesOpened_) {
    openCSVFiles();
    writeCSVHeaders();
    filesOpened_ = true;
  }
  
  edm::LogInfo("HLSDigiExporter") 
      << "Processing Event: " << currentEvent_ << " Run: " << currentRun_;
}

void HLSDigiExporter::observeEventEnd(const edm::Event& iEvent, 
                                     std::unique_ptr<l1t::RegionalMuonCandBxCollection>& finalCandidates) {
  // Flush all files
  if (dtPhiDigiFile_.is_open()) dtPhiDigiFile_.flush();
  if (dtThetaDigiFile_.is_open()) dtThetaDigiFile_.flush();
  if (cscDigiFile_.is_open()) cscDigiFile_.flush();
  if (rpcDigiFile_.is_open()) rpcDigiFile_.flush();
  if (goldenResultsFile_.is_open()) goldenResultsFile_.flush();
  if (refHitsFile_.is_open()) refHitsFile_.flush();
  if (gpResultsFile_.is_open()) gpResultsFile_.flush();
  if (gpFinalResultsFile_.is_open()) gpFinalResultsFile_.flush();
  if (sortedCandidatesFile_.is_open()) sortedCandidatesFile_.flush();
  if (stubsFile_.is_open()) stubsFile_.flush();
}

void HLSDigiExporter::observeProcesorBegin(unsigned int iProcessor, l1t::tftype mtfType) {
  currentProcessor_ = iProcessor;
  currentMtfType_ = mtfType;
}

void HLSDigiExporter::addProcesorData(std::string key, boost::property_tree::ptree& procDataTree) {
  // This is where we intercept the XML data and convert it to CSV
  if (key == "linkData") {
    // DT digis come through linkData
    exportDTPhiDigis(procDataTree);
    exportDTThetaDigis(procDataTree);
    // Also export golden results (converted stubs) that come with DT data
    exportGoldenResults(procDataTree);
  } else if (key == "CSCdigis") {
    // CSC digis come through CSCdigis key
    exportCSCDigis(procDataTree);
  } else if (key == "CSCstubs") {
    // CSC stubs come through CSCstubs key - handled in stubs.csv
    exportStubs(procDataTree);
  } else if (key == "RPCdigis") {
    // RPC digis come through RPCdigis key
    exportRPCDigis(procDataTree);
  } else if (key == "RPCstubs") {
    // RPC stubs come through RPCstubs key - handled in stubs.csv
    exportStubs(procDataTree);
  } else if (key == "DTstubs") {
    // DT stubs come through DTstubs key - handled in stubs.csv
    exportStubs(procDataTree);
  }
}

void HLSDigiExporter::endJob() {
  edm::LogInfo("HLSDigiExporter") << "HLS Digi Export completed. Files saved in: " << outputDir_;
}

void HLSDigiExporter::openCSVFiles() {
  std::string dtPhiFile = outputDir_ + "/dt_phi_digis.csv";
  std::string dtThetaFile = outputDir_ + "/dt_theta_digis.csv";
  std::string cscFile = outputDir_ + "/csc_digis.csv";
  std::string rpcFile = outputDir_ + "/rpc_digis.csv";
  std::string goldenFile = outputDir_ + "/golden_results.csv";
  std::string refHitsFile = outputDir_ + "/reference_hits.csv";
  std::string gpResultsFile = outputDir_ + "/gp_processing_results.csv";
  std::string gpFinalResultsFile = outputDir_ + "/gp_final_results.csv";
  std::string sortedCandidatesFile = outputDir_ + "/sorted_candidates.csv";
  std::string stubsFile = outputDir_ + "/stubs.csv";
  
  dtPhiDigiFile_.open(dtPhiFile);
  dtThetaDigiFile_.open(dtThetaFile);
  cscDigiFile_.open(cscFile);
  rpcDigiFile_.open(rpcFile);
  goldenResultsFile_.open(goldenFile);
  refHitsFile_.open(refHitsFile);
  gpResultsFile_.open(gpResultsFile);
  gpFinalResultsFile_.open(gpFinalResultsFile);
  sortedCandidatesFile_.open(sortedCandidatesFile);
  stubsFile_.open(stubsFile);
  
  if (!dtPhiDigiFile_.is_open() || !dtThetaDigiFile_.is_open() || 
      !cscDigiFile_.is_open() || !rpcDigiFile_.is_open() || 
      !goldenResultsFile_.is_open() || !refHitsFile_.is_open() || 
      !gpResultsFile_.is_open() || !gpFinalResultsFile_.is_open() ||
      !sortedCandidatesFile_.is_open() || !stubsFile_.is_open()) {
    edm::LogError("HLSDigiExporter") << "Failed to open CSV files!";
  } else {
    edm::LogInfo("HLSDigiExporter") << "Opened CSV files for digi export";
  }
}

void HLSDigiExporter::writeCSVHeaders() {
  // DT Phi Digi header
  dtPhiDigiFile_ << "event,run,processor,tftype,dtID,whNum,scNum,stNum,slNum,quality,rpcFlag,phi,phiBend,bx\n";
  
  // DT Theta Digi header  
  dtThetaDigiFile_ << "event,run,processor,tftype,dtID,whNum,scNum,stNum,quality,rpcFlag,k,z,bx\n";
  
  // CSC Digi header - comprehensive fields (cscID already included)
  cscDigiFile_ << "event,run,processor,tftype,detId,endcap,station,ring,chamber,layer,"
               << "trknmb,valid,quality,keywire,strip,pattern,bend,bx,mpclink,bx0,syncErr,cscID,"
               << "isRun3,quartStripBit,eighthStripBit,run3Pattern,slope,hmt,"
               << "fractionalStrip,fractionalSlope,clctPattern,stripType,bxData,type,"
               << "offset,scale,order,halfStrip\n";
  
  // RPC Digi header - comprehensive fields
  rpcDigiFile_ << "event,run,processor,tftype,rpcID,region,ring,station,sector,layer,subsector,roll,"
               << "strip,bx,time,coordinateX,coordinateY,deltaTime,deltaX,deltaY,"
               << "hasTime,hasX,hasY,isPseudoDigi\n";
               
  // Golden Results header - MuonStub objects after conversion
  goldenResultsFile_ << "event,run,processor,tftype,type,logicLayer,phiHw,etaHw,qualityHw,"
                     << "phiBHw,bx,timing,r,detId,hwName\n";
                     
    // Reference Hits header - RefHit and corresponding restricted stubs per layer
  refHitsFile_ << "event,run,processor,refHitIndex,refHitLayer,refHitInput,refHitRegion,"
               << "refHitPhiMin,refHitPhiMax,allLayersRestrictedStubsData\n";
               
  // Golden Pattern Results header - StubResult from each GP processing
  gpResultsFile_ << "event,run,processor,refHitIndex,refHitLayer,refHitInput,refHitRegion,"
                 << "gpIndex,gpKey,layer,pdfValue,firedFlag,pdfBin,deltaPhiValue,"
                 << "selectedStubPhi,selectedStubEta,selectedStubQuality,selectedStubDetId,phiDistMin\n";
                 
  // Golden Pattern Final Results header - GoldenPatternResult after finalise
  gpFinalResultsFile_ << "event,run,processor,gpIndex,gpNumber,gpEtaCode,gpPt,gpCharge,"
                      << "refHitIndex,refLayer,phi,eta,refHitPhi,valid,pdfSum,pdfSumUnconstr,"
                      << "firedLayerCnt,firedLayerBits,gpProbability1,gpProbability2\n";
                      
  // Sorted Candidates header - AlgoMuon winners after sorting
  sortedCandidatesFile_ << "event,run,processor,tftype,candidateIndex,refLayer,phi,eta,pt,ptUnconstr,"
                        << "charge,quality,disc,pdfSum,pdfSumConstr,pdfSumUnconstr,firedLayerCnt,"
                        << "firedLayerBits,hwPatternNumber,refHitNumber\n";
                        
  // Stubs header - Exported stubs with lookup table etaHw values
  stubsFile_ << "event,run,processor,tftype,type,logicLayer,phiHw,etaHw,qualityHw,phiBHw,bx,timing,r,detId,hwName\n";
}

void HLSDigiExporter::exportDTPhiDigis(const boost::property_tree::ptree& procDataTree) {
  try {
    // Iterate through all dtP2PhiDigi entries in the procDataTree
    for (const auto& child : procDataTree) {
      if (child.first == "dtP2PhiDigi" || child.first == "dtPhiDigi") {
        const auto& digi = child.second;
        
        dtPhiDigiFile_ << currentEvent_ << ","
                       << currentRun_ << ","
                       << currentProcessor_ << ","
                       << (currentMtfType_ == l1t::omtf_neg ? "NEG" : 
                          (currentMtfType_ == l1t::omtf_pos ? "POS" : "BARREL")) << ","
                       << digi.get<unsigned int>("<xmlattr>.dtID", 0) << ","
                       << digi.get<int>("<xmlattr>.whNum") << ","
                       << digi.get<int>("<xmlattr>.scNum") << ","
                       << digi.get<int>("<xmlattr>.stNum") << ","
                       << digi.get<int>("<xmlattr>.slNum") << ","
                       << digi.get<int>("<xmlattr>.quality") << ","
                       << digi.get<int>("<xmlattr>.rpcFlag") << ","
                       << digi.get<int>("<xmlattr>.phi") << ","
                       << digi.get<int>("<xmlattr>.phiBend") << ","
                       << digi.get<int>("<xmlattr>.bx", 0) << "\n"; // bx might not always be present
      }
    }
  } catch (const std::exception& e) {
    edm::LogWarning("HLSDigiExporter") << "Error exporting DT Phi digis: " << e.what();
  }
}

void HLSDigiExporter::exportDTThetaDigis(const boost::property_tree::ptree& procDataTree) {
  try {
    for (const auto& child : procDataTree) {
      if (child.first == "dtP2ThDigi" || child.first == "dtThDigi") {
        const auto& digi = child.second;
        
        dtThetaDigiFile_ << currentEvent_ << ","
                         << currentRun_ << ","
                         << currentProcessor_ << ","
                         << (currentMtfType_ == l1t::omtf_neg ? "NEG" : 
                            (currentMtfType_ == l1t::omtf_pos ? "POS" : "BARREL")) << ","
                         << digi.get<unsigned int>("<xmlattr>.dtID", 0) << ","
                         << digi.get<int>("<xmlattr>.whNum") << ","
                         << digi.get<int>("<xmlattr>.scNum") << ","
                         << digi.get<int>("<xmlattr>.stNum") << ","
                         << digi.get<int>("<xmlattr>.quality") << ","
                         << digi.get<int>("<xmlattr>.rpcFlag") << ","
                         << digi.get<int>("<xmlattr>.k") << ","
                         << digi.get<int>("<xmlattr>.z") << ","
                         << digi.get<int>("<xmlattr>.bx", 0) << "\n";
      }
    }
  } catch (const std::exception& e) {
    edm::LogWarning("HLSDigiExporter") << "Error exporting DT Theta digis: " << e.what();
  }
}

void HLSDigiExporter::exportCSCDigis(const boost::property_tree::ptree& procDataTree) {
  try {
    for (const auto& child : procDataTree) {
      if (child.first == "cscDigi" || child.first == "cscCorrelatedLCTDigi") {
        const auto& digi = child.second;
        
        cscDigiFile_ << currentEvent_ << ","
                     << currentRun_ << ","
                     << currentProcessor_ << ","
                     << (currentMtfType_ == l1t::omtf_neg ? "NEG" : 
                        (currentMtfType_ == l1t::omtf_pos ? "POS" : "BARREL")) << ","
                     // Detector ID fields
                     << digi.get<unsigned long>("<xmlattr>.detId", 0) << ","
                     << digi.get<int>("<xmlattr>.endcap", 0) << ","
                     << digi.get<int>("<xmlattr>.station", 0) << ","
                     << digi.get<int>("<xmlattr>.ring", 0) << ","
                     << digi.get<int>("<xmlattr>.chamber", 0) << ","
                     << digi.get<int>("<xmlattr>.layer", 0) << ","
                     // Core LCT data fields
                     << digi.get<int>("<xmlattr>.trknmb", 0) << ","
                     << digi.get<bool>("<xmlattr>.valid", false) << ","
                     << digi.get<int>("<xmlattr>.quality", 0) << ","
                     << digi.get<int>("<xmlattr>.keywire", 0) << ","
                     << digi.get<int>("<xmlattr>.strip", 0) << ","
                     << digi.get<int>("<xmlattr>.pattern", 0) << ","
                     << digi.get<int>("<xmlattr>.bend", 0) << ","
                     << digi.get<int>("<xmlattr>.bx", 0) << ","
                     << digi.get<int>("<xmlattr>.mpclink", 0) << ","
                     << digi.get<int>("<xmlattr>.bx0", 0) << ","
                     << digi.get<int>("<xmlattr>.syncErr", 0) << ","
                     << digi.get<int>("<xmlattr>.cscID", 0) << ","
                     // Run-3 specific fields
                     << digi.get<bool>("<xmlattr>.isRun3", false) << ","
                     << digi.get<bool>("<xmlattr>.quartStripBit", false) << ","
                     << digi.get<bool>("<xmlattr>.eighthStripBit", false) << ","
                     << digi.get<int>("<xmlattr>.run3Pattern", 0) << ","
                     << digi.get<int>("<xmlattr>.slope", 0) << ","
                     << digi.get<int>("<xmlattr>.hmt", 0) << ","
                     // Additional computed fields
                     << digi.get<float>("<xmlattr>.fractionalStrip", 0.0) << ","
                     << digi.get<float>("<xmlattr>.fractionalSlope", 0.0) << ","
                     << digi.get<int>("<xmlattr>.clctPattern", 0) << ","
                     << digi.get<int>("<xmlattr>.stripType", 0) << ","
                     << digi.get<int>("<xmlattr>.bxData", 0) << ","
                     << digi.get<int>("<xmlattr>.type", 0) << ","
                     // CSC conversion parameters
                     << digi.get<int>("<xmlattr>.offset", 0) << ","
                     << digi.get<double>("<xmlattr>.scale", 0.0) << ","
                     << digi.get<int>("<xmlattr>.order", 0) << ","
                     << digi.get<int>("<xmlattr>.halfStrip", 0) << "\n";
      }
    }
  } catch (const std::exception& e) {
    edm::LogWarning("HLSDigiExporter") << "Error exporting CSC digis: " << e.what();
  }
}

void HLSDigiExporter::exportRPCDigis(const boost::property_tree::ptree& procDataTree) {
  try {
    for (const auto& child : procDataTree) {
      if (child.first == "rpcDigi") {
        const auto& digi = child.second;
        
        rpcDigiFile_ << currentEvent_ << ","
                     << currentRun_ << ","
                     << currentProcessor_ << ","
                     << (currentMtfType_ == l1t::omtf_neg ? "NEG" : 
                        (currentMtfType_ == l1t::omtf_pos ? "POS" : "BARREL")) << ","
                     // Detector ID 
                     << digi.get<unsigned int>("<xmlattr>.rpcID", 0) << ","
                     // Detector ID fields
                     << digi.get<int>("<xmlattr>.region", 0) << ","
                     << digi.get<int>("<xmlattr>.ring", 0) << ","
                     << digi.get<int>("<xmlattr>.station", 0) << ","
                     << digi.get<int>("<xmlattr>.sector", 0) << ","
                     << digi.get<int>("<xmlattr>.layer", 0) << ","
                     << digi.get<int>("<xmlattr>.subsector", 0) << ","
                     << digi.get<int>("<xmlattr>.roll", 0) << ","
                     // Core RPC digi fields
                     << digi.get<int>("<xmlattr>.strip", 0) << ","
                     << digi.get<int>("<xmlattr>.bx", 0) << ","
                     // Timing and spatial information
                     << digi.get<double>("<xmlattr>.time", 0.0) << ","
                     << digi.get<double>("<xmlattr>.coordinateX", 0.0) << ","
                     << digi.get<double>("<xmlattr>.coordinateY", 0.0) << ","
                     << digi.get<double>("<xmlattr>.deltaTime", 0.0) << ","
                     << digi.get<double>("<xmlattr>.deltaX", 0.0) << ","
                     << digi.get<double>("<xmlattr>.deltaY", 0.0) << ","
                     // Flags for available information
                     << digi.get<bool>("<xmlattr>.hasTime", false) << ","
                     << digi.get<bool>("<xmlattr>.hasX", false) << ","
                     << digi.get<bool>("<xmlattr>.hasY", false) << ","
                     << digi.get<bool>("<xmlattr>.isPseudoDigi", false) << "\n";
      }
    }
  } catch (const std::exception& e) {
    edm::LogWarning("HLSDigiExporter") << "Error exporting RPC digis: " << e.what();
  }
}

void HLSDigiExporter::exportGoldenResults(const boost::property_tree::ptree& procDataTree) {
  try {
    for (const auto& child : procDataTree) {
      if (child.first == "goldenStub") {
        const auto& stub = child.second;
        
        goldenResultsFile_ << currentEvent_ << ","
                           << currentRun_ << ","
                           << currentProcessor_ << ","
                           << (currentMtfType_ == l1t::omtf_neg ? "NEG" : 
                              (currentMtfType_ == l1t::omtf_pos ? "POS" : "BARREL")) << ","
                           << stub.get<int>("<xmlattr>.type", 0) << ","
                           << stub.get<int>("<xmlattr>.logicLayer", 0) << ","
                           << stub.get<int>("<xmlattr>.phiHw", 0) << ","
                           << stub.get<int>("<xmlattr>.etaHw", 0) << ","
                           << stub.get<int>("<xmlattr>.qualityHw", 0) << ","
                           << stub.get<int>("<xmlattr>.phiBHw", 0) << ","
                           << stub.get<int>("<xmlattr>.bx", 0) << ","
                           << stub.get<int>("<xmlattr>.timing", 0) << ","
                           << stub.get<int>("<xmlattr>.r", 0) << ","
                           << stub.get<unsigned long>("<xmlattr>.detId", 0) << ","
                           << stub.get<std::string>("<xmlattr>.hwName", "Unknown") << "\n";
      }
    }
  } catch (const std::exception& e) {
    edm::LogWarning("HLSDigiExporter") << "Error exporting golden results: " << e.what();
  }
}

void HLSDigiExporter::createOutputDirectory() {
  boost::filesystem::path dir(outputDir_);
  if (!boost::filesystem::exists(dir)) {
    boost::filesystem::create_directories(dir);
    edm::LogInfo("HLSDigiExporter") << "Created output directory: " << outputDir_;
  }
}

void HLSDigiExporter::observeRefHitProcessing(unsigned int iProcessor,
                                              unsigned int iRefHit,
                                              const RefHitDef& refHitDef,
                                              const std::vector<std::pair<unsigned int, MuonStubPtrs1D>>& allLayerStubs,
                                              const std::vector<std::pair<unsigned int, std::vector<int>>>& allLayerExtrapolatedPhi,
                                              const std::vector<std::pair<unsigned int, std::vector<unsigned int>>>& allLayerHwNumbers) {
  exportRefHitsEntry(iProcessor, iRefHit, refHitDef, allLayerStubs, allLayerExtrapolatedPhi, allLayerHwNumbers);
}

void HLSDigiExporter::observeGoldenPatternResults(unsigned int iProcessor,
                                                  unsigned int iRefHit,
                                                  const RefHitDef& refHitDef,
                                                  unsigned int iGP,
                                                  unsigned int iLayer,
                                                  const StubResult& stubResult,
                                                  int phiDistMin) {
  exportGPResultsEntry(iProcessor, iRefHit, refHitDef, iGP, iLayer, stubResult, phiDistMin);
}

void HLSDigiExporter::exportRefHitsEntry(unsigned int iProcessor,
                                         unsigned int iRefHit,
                                         const RefHitDef& refHitDef,
                                         const std::vector<std::pair<unsigned int, MuonStubPtrs1D>>& allLayerStubs,
                                         const std::vector<std::pair<unsigned int, std::vector<int>>>& allLayerExtrapolatedPhi,
                                         const std::vector<std::pair<unsigned int, std::vector<unsigned int>>>& allLayerHwNumbers) {
  if (!refHitsFile_.is_open()) {
    edm::LogWarning("HLSDigiExporter") << "Reference hits file not open!";
    return;
  }

  // Build combined stubs data string from all layers with extrapolated phi
  std::stringstream allStubsData;
  allStubsData << "[";
  bool firstStub = true;
  unsigned int totalStubCount = 0;
  
  for (const auto& layerPair : allLayerStubs) {
    unsigned int layerNum = layerPair.first;
    const MuonStubPtrs1D& restrictedStubs = layerPair.second;
    
    // Find corresponding extrapolated phi data for this layer
    std::vector<int> extrapolatedPhi;
    for (const auto& phiData : allLayerExtrapolatedPhi) {
      if (phiData.first == layerNum) {
        extrapolatedPhi = phiData.second;
        break;
      }
    }
    // Get hardware layer numbers for this layer
    std::vector<unsigned int> hwNumbers;
    for (const auto& hwData : allLayerHwNumbers) {
      if (hwData.first == layerNum) {
        hwNumbers = hwData.second;
        break;
      }
    }
    
    for (size_t i = 0; i < restrictedStubs.size(); ++i) {
      if (restrictedStubs[i]) {
        if (!firstStub) allStubsData << ",";
        allStubsData << "{layer:" << layerNum
                    << ",phi:" << restrictedStubs[i]->phiHw
                    << ",eta:" << restrictedStubs[i]->etaHw
                    << ",qual:" << restrictedStubs[i]->qualityHw
                    << ",logicLayer:" << restrictedStubs[i]->logicLayer
                    << ",detId:" << restrictedStubs[i]->detId;
        
        // Add extrapolated phi if available
        if (i < extrapolatedPhi.size()) {
          allStubsData << ",phiExtr:" << extrapolatedPhi[i];
        } else {
          allStubsData << ",phiExtr:0";  // Default value when no extrapolation
        }
        
        // Add hwName using proper hardware layer mapping from OMTFConfiguration
        std::string hwName = "Unknown";
        if (i < hwNumbers.size() && hwNumbers[i] != 0) {
          hwName = getHwNameFromHwNumber(hwNumbers[i]);
        }
        allStubsData << ",hwName:" << hwName << "}";
        firstStub = false;
        totalStubCount++;
      }
    }
  }
  allStubsData << "]";

  // Write single row to CSV with all layer data combined
  refHitsFile_ << currentEvent_ << ","
               << currentRun_ << ","
               << iProcessor << ","
               << iRefHit << ","
               << refHitDef.iRefLayer << ","
               << refHitDef.iInput << ","
               << refHitDef.iRegion << ","
               << refHitDef.range.first << ","
               << refHitDef.range.second << ","
               << totalStubCount << ","
               << "\"" << allStubsData.str() << "\"\n";
}

void HLSDigiExporter::exportGPResultsEntry(unsigned int iProcessor,
                                           unsigned int iRefHit,
                                           const RefHitDef& refHitDef,
                                           unsigned int iGP,
                                           unsigned int iLayer,
                                           const StubResult& stubResult,
                                           int phiDistMin) {
  if (!gpResultsFile_.is_open()) {
    edm::LogWarning("HLSDigiExporter") << "GP Results file not open!";
    return;
  }

  // Write GP processing result to CSV
  gpResultsFile_ << currentEvent_ << ","
                 << currentRun_ << ","
                 << iProcessor << ","
                 << iRefHit << ","
                 << refHitDef.iRefLayer << ","
                 << refHitDef.iInput << ","
                 << refHitDef.iRegion << ","
                 << iGP << ","
                 << iGP << "," // Use GP number as GP key for now
                 << iLayer << ","
                 << stubResult.getPdfVal() << ","
                 << (stubResult.getValid() ? 1 : 0) << ","
                 << stubResult.getPdfBin() << ","
                 << stubResult.getDeltaPhi() << ",";
  
  // Add selected stub information if available
  if (stubResult.getMuonStub()) {
    gpResultsFile_ << stubResult.getMuonStub()->phiHw << ","
                   << stubResult.getMuonStub()->etaHw << ","
                   << stubResult.getMuonStub()->qualityHw << ","
                   << stubResult.getMuonStub()->detId << ",";
  } else {
    gpResultsFile_ << ",,,,";  // Empty stub fields
  }
  
  gpResultsFile_ << phiDistMin << "\n";
}

void HLSDigiExporter::observeGoldenPatternFinalResults(unsigned int iProcessor,
                                                       unsigned int iGP,
                                                       const Key& gpKey,
                                                       unsigned int iRefHit,
                                                       const GoldenPatternResult& gpResult) {
  exportGPFinalResultsEntry(iProcessor, iGP, gpKey, iRefHit, gpResult);
}

void HLSDigiExporter::exportGPFinalResultsEntry(unsigned int iProcessor,
                                                 unsigned int iGP,
                                                 const Key& gpKey,
                                                 unsigned int iRefHit,
                                                 const GoldenPatternResult& gpResult) {
  if (!gpFinalResultsFile_.is_open()) {
    edm::LogWarning("HLSDigiExporter") << "GP Final Results file not open!";
    return;
  }

  // Filter out entries with refLayer = -1
  if (gpResult.getRefLayer() == -1) {
    return;
  }

  // Format fired layer bits as 18-bit one-hot vector
  std::string firedLayerBitsStr = "";
  unsigned int firedLayerBits = gpResult.getFiredLayerBits();
  for (int bit = 17; bit >= 0; bit--) {
    firedLayerBitsStr += ((firedLayerBits & (1 << bit)) ? "1" : "0");
  }

  // Write GP final result to CSV
  gpFinalResultsFile_ << currentEvent_ << ","
                      << currentRun_ << ","
                      << iProcessor << ","
                      << iGP << ","
                      << gpKey.theNumber << ","
                      << gpKey.theEtaCode << ","
                      << gpKey.thePt << ","
                      << gpKey.theCharge << ","
                      << iRefHit << ","
                      << gpResult.getRefLayer() << ","
                      << gpResult.getPhi() << ","
                      << gpResult.getEta() << ","
                      << gpResult.getRefHitPhi() << ","
                      << (gpResult.isValid() ? 1 : 0) << ","
                      << gpResult.getPdfSum() << ","
                      << gpResult.getPdfSumUnconstr() << ","
                      << gpResult.getFiredLayerCnt() << ","
                      << firedLayerBitsStr << ","
                      << gpResult.getGpProbability1() << ","
                      << gpResult.getGpProbability2() << "\n";
}

void HLSDigiExporter::observeSortedCandidates(unsigned int iProcessor,
                                              l1t::tftype mtfType,
                                              const AlgoMuons& algoCandidates) {
  for (unsigned int iCandidate = 0; iCandidate < algoCandidates.size(); iCandidate++) {
    if (algoCandidates[iCandidate] && algoCandidates[iCandidate]->isValid()) {
      exportSortedCandidateEntry(iProcessor, mtfType, iCandidate, algoCandidates[iCandidate]);
    }
  }
}

void HLSDigiExporter::exportSortedCandidateEntry(unsigned int iProcessor,
                                                 l1t::tftype mtfType,
                                                 unsigned int iCandidate,
                                                 const AlgoMuonPtr& algoMuon) {
  if (!sortedCandidatesFile_.is_open()) {
    edm::LogWarning("HLSDigiExporter") << "Sorted Candidates file not open!";
    return;
  }

  // Convert tftype to string
  std::string tfTypeStr = (mtfType == l1t::omtf_neg ? "NEG" : 
                          (mtfType == l1t::omtf_pos ? "POS" : "BARREL"));

  // Format fired layer bits as 18-bit binary string
  std::bitset<18> firedLayerBitset(algoMuon->getFiredLayerBits());

  // Write sorted candidate to CSV
  sortedCandidatesFile_ << currentEvent_ << ","
                        << currentRun_ << ","
                        << iProcessor << ","
                        << tfTypeStr << ","
                        << iCandidate << ","
                        << algoMuon->getRefLayer() << ","
                        << algoMuon->getPhi() << ","
                        << algoMuon->getEtaHw() << ","
                        << algoMuon->getPtConstr() << ","
                        << algoMuon->getPtUnconstr() << ","
                        << algoMuon->getChargeConstr() << ","
                        << algoMuon->getQ() << ","
                        << algoMuon->getDisc() << ","
                        << algoMuon->getPdfSum() << ","
                        << algoMuon->getPdfSumConstr() << ","
                        << algoMuon->getGpResultUnconstr().getPdfSumUnconstr() << ","
                        << algoMuon->getFiredLayerCnt() << ","
                        << firedLayerBitset << ","
                        << algoMuon->getHwPatternNumConstr() << ","
                        << algoMuon->getRefHitNumber() << "\n";
}

void HLSDigiExporter::exportStubs(const boost::property_tree::ptree& procDataTree) {
  std::cout << "DEBUG: exportStubs called with " << procDataTree.size() << " entries" << std::endl;
  
  try {
    for (const auto& child : procDataTree) {
      std::cout << "DEBUG: Processing stub child: " << child.first << std::endl;
      
      // Use the same logic as exportGoldenResults but for different stub types
      if (child.first == "CSCstub" || child.first == "RPCstub" || child.first == "DTstub") {
        const auto& stub = child.second;
        
        int etaHw = stub.get<int>("<xmlattr>.etaHw", 0);
        std::cout << "DEBUG: Exporting " << child.first << " with etaHw=" << etaHw << std::endl;
        
        stubsFile_ << currentEvent_ << ","
                   << currentRun_ << ","
                   << currentProcessor_ << ","
                   << (currentMtfType_ == l1t::omtf_neg ? "NEG" : 
                      (currentMtfType_ == l1t::omtf_pos ? "POS" : "BARREL")) << ","
                   << stub.get<int>("<xmlattr>.type", 0) << ","
                   << stub.get<int>("<xmlattr>.logicLayer", 0) << ","
                   << stub.get<int>("<xmlattr>.phiHw", 0) << ","
                   << etaHw << ","
                   << stub.get<int>("<xmlattr>.qualityHw", 0) << ","
                   << stub.get<int>("<xmlattr>.phiBHw", 0) << ","
                   << stub.get<int>("<xmlattr>.bx", 0) << ","
                   << stub.get<int>("<xmlattr>.timing", 0) << ","
                   << stub.get<int>("<xmlattr>.r", 0) << ","
                   << stub.get<unsigned long>("<xmlattr>.detId", 0) << ","
                   << stub.get<std::string>("<xmlattr>.hwName", "Unknown") << "\n";
      }
    }
    stubsFile_.flush();  // Ensure data is written immediately
  } catch (const std::exception& e) {
    edm::LogWarning("HLSDigiExporter") << "Error exporting stubs: " << e.what();
  }
}