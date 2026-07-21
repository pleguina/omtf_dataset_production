#ifndef L1T_OmtfP1_HLSTESTDATAGENERATOR_H_
#define L1T_OmtfP1_HLSTESTDATAGENERATOR_H_

#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/IOMTFEmulationObserver.h"
#include "FWCore/Framework/interface/Event.h"

#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <memory>

/**
 * HLS Test Data Generator
 * 
 * This class generates CSV input/output pairs for HLS verification.
 * It captures data at specific algorithm checkpoints to create golden reference vectors.
 * 
 * Usage:
 * 1. Register capture points with addCapturePoint()
 * 2. Call captureInputOutput() at algorithm checkpoints
 * 3. CSV files are automatically generated per event per capture point
 */

class HLSTestDataGenerator : public IOMTFEmulationObserver {
public:
  HLSTestDataGenerator(const std::string& outputDir = "hls_test_data");
  ~HLSTestDataGenerator() override;

  // Observer interface
  void observeEventBegin(const edm::Event& iEvent) override;
  void observeEventEnd(const edm::Event& iEvent, 
                       std::unique_ptr<l1t::RegionalMuonCandBxCollection>& finalCandidates) override;
  void observeProcesorEmulation(unsigned int iProcessor,
                                l1t::tftype mtfType,
                                const std::shared_ptr<OMTFinput>& input,
                                const AlgoMuons& algoCandidates,
                                const AlgoMuons& gbCandidates,
                                const FinalMuons& finalMuons) override;
  void endJob() override;

  // Main capture interface - call this at algorithm checkpoints
  template<typename InputType, typename OutputType>
  void captureInputOutput(const std::string& functionName,
                          unsigned int iProcessor,
                          l1t::tftype mtfType,
                          const InputType& inputs,
                          const OutputType& outputs);

  // Register a capture point for a specific function
  void addCapturePoint(const std::string& functionName,
                       const std::vector<std::string>& inputHeaders,
                       const std::vector<std::string>& outputHeaders);

  // Specialized capture methods for common OMTF data types
  void captureStubResults(const std::string& functionName,
                          unsigned int iProcessor,
                          l1t::tftype mtfType,
                          const std::vector<MuonStubPtr>& inputStubs,
                          const std::vector<StubResult>& stubResults);

  void captureGoldenPatternResults(const std::string& functionName,
                                   unsigned int iProcessor,
                                   l1t::tftype mtfType,
                                   const OMTFinput& input,
                                   const std::vector<GoldenPatternResult>& gpResults);

  void captureGhostBusting(const std::string& functionName,
                           unsigned int iProcessor,
                           l1t::tftype mtfType,
                           const AlgoMuons& inputCandidates,
                           const AlgoMuons& outputCandidates);

private:
  struct CapturePoint {
    std::vector<std::string> inputHeaders;
    std::vector<std::string> outputHeaders;
    std::ofstream inputFile;
    std::ofstream outputFile;
    bool isOpen = false;
  };

  std::string outputDir_;
  unsigned int currentEvent_;
  unsigned int currentRun_;
  
  // Map: functionName -> CapturePoint
  std::map<std::string, CapturePoint> capturePoints_;
  
  // Helper methods
  std::string makeFileName(const std::string& functionName, 
                           const std::string& type, // "input" or "output"
                           unsigned int iProcessor,
                           l1t::tftype mtfType);
  
  void openCapturePoint(const std::string& functionName,
                        unsigned int iProcessor,
                        l1t::tftype mtfType);
  
  void writeEventHeader(std::ofstream& file, 
                        unsigned int iProcessor,
                        l1t::tftype mtfType);
  
  template<typename T>
  void writeCSVRow(std::ofstream& file, const std::vector<T>& data);
  
  void createOutputDirectory();
};

// Template implementations
template<typename InputType, typename OutputType>
void HLSTestDataGenerator::captureInputOutput(const std::string& functionName,
                                              unsigned int iProcessor,
                                              l1t::tftype mtfType,
                                              const InputType& inputs,
                                              const OutputType& outputs) {
  auto it = capturePoints_.find(functionName);
  if (it == capturePoints_.end()) {
    edm::LogWarning("HLSTestDataGenerator") 
        << "Capture point not registered: " << functionName;
    return;
  }

  if (!it->second.isOpen) {
    openCapturePoint(functionName, iProcessor, mtfType);
  }

  // Write event metadata
  writeEventHeader(it->second.inputFile, iProcessor, mtfType);
  writeEventHeader(it->second.outputFile, iProcessor, mtfType);

  // Serialize input data
  std::vector<std::string> inputData = serializeData(inputs);
  writeCSVRow(it->second.inputFile, inputData);

  // Serialize output data  
  std::vector<std::string> outputData = serializeData(outputs);
  writeCSVRow(it->second.outputFile, outputData);
}

template<typename T>
void HLSTestDataGenerator::writeCSVRow(std::ofstream& file, const std::vector<T>& data) {
  for (size_t i = 0; i < data.size(); ++i) {
    file << data[i];
    if (i < data.size() - 1) file << ",";
  }
  file << std::endl;
}

#endif /* L1T_OmtfP1_HLSTESTDATAGENERATOR_H_ */