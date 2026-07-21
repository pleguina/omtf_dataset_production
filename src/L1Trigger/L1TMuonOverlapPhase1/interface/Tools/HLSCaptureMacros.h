/*
 * HLSCaptureMacros.h
 * 
 * Convenient macros for adding HLS capture points to existing code
 */

#ifndef L1T_OmtfP1_HLSCAPTUREMACROS_H_
#define L1T_OmtfP1_HLSCAPTUREMACROS_H_

// Enable/disable HLS capture at compile time
#ifdef ENABLE_HLS_CAPTURE
  #define HLS_CAPTURE_ENABLED true
#else
  #define HLS_CAPTURE_ENABLED false
#endif

// Macro to add HLS capture point at any location in code
#define HLS_CAPTURE_POINT(hlsGen, functionName, iProcessor, mtfType, inputs, outputs) \
  do { \
    if (HLS_CAPTURE_ENABLED && hlsGen) { \
      hlsGen->captureInputOutput(functionName, iProcessor, mtfType, inputs, outputs); \
    } \
  } while(0)

// Macro to capture stub-based operations
#define HLS_CAPTURE_STUBS(hlsGen, functionName, iProcessor, mtfType, inputStubs, outputStubs) \
  do { \
    if (HLS_CAPTURE_ENABLED && hlsGen) { \
      hlsGen->captureStubResults(functionName, iProcessor, mtfType, inputStubs, outputStubs); \
    } \
  } while(0)

// Macro to capture ghost busting operations
#define HLS_CAPTURE_GHOSTBUST(hlsGen, functionName, iProcessor, mtfType, inputCands, outputCands) \
  do { \
    if (HLS_CAPTURE_ENABLED && hlsGen) { \
      hlsGen->captureGhostBusting(functionName, iProcessor, mtfType, inputCands, outputCands); \
    } \
  } while(0)

// Example of how to modify existing algorithm functions:

/*
 * BEFORE (original algorithm):
 * 
 * AlgoMuons OMTFProcessor::ghostBust(const AlgoMuons& muonsIN) {
 *   // ghost busting logic
 *   AlgoMuons result = performGhostBusting(muonsIN);
 *   return result;
 * }
 * 
 * AFTER (with HLS capture):
 * 
 * AlgoMuons OMTFProcessor::ghostBust(const AlgoMuons& muonsIN) {
 *   // ghost busting logic  
 *   AlgoMuons result = performGhostBusting(muonsIN);
 *   
 *   // Add HLS capture point
 *   HLS_CAPTURE_GHOSTBUST(hlsGenerator_, "ghostBust", currentProcessor_, currentMtfType_, muonsIN, result);
 *   
 *   return result;
 * }
 */

// Structure for passing multiple inputs/outputs
template<typename... Args>
struct HLSDataPacket {
  std::tuple<Args...> data;
  
  HLSDataPacket(Args... args) : data(std::make_tuple(args...)) {}
  
  template<size_t N>
  auto get() const -> decltype(std::get<N>(data)) {
    return std::get<N>(data);
  }
};

// Helper to create data packets
template<typename... Args>
auto makeHLSPacket(Args... args) -> HLSDataPacket<Args...> {
  return HLSDataPacket<Args...>(args...);
}

#endif /* L1T_OmtfP1_HLSCAPTUREMACROS_H_ */