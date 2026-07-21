#ifndef L1T_OmtfP1_OMTFinputMaker_H
#define L1T_OmtfP1_OMTFinputMaker_H

#include "FWCore/Framework/interface/EventSetup.h"

#include "DataFormats/L1DTTrackFinder/interface/L1MuDTChambPhContainer.h"
#include "DataFormats/L1DTTrackFinder/interface/L1MuDTChambThContainer.h"
#include "DataFormats/CSCDigi/interface/CSCCorrelatedLCTDigiCollection.h"
#include "DataFormats/RPCDigi/interface/RPCDigiCollection.h"

#include "L1Trigger/L1TMuonOverlapPhase1/interface/MuonStubMakerBase.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/OmtfAngleConverter.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/OMTFinput.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/OMTFConfiguration.h"
#include "L1Trigger/L1TMuonOverlapPhase1/interface/RpcClusterization.h"

#include <vector>
#include <cstdint>
#include <memory>

// Helper function to map hwNumber to hwName based on the layer mapping
std::string getHwNameFromHwNumber(unsigned int hwNumber);

class DtDigiToStubsConverterOmtf : public DtDigiToStubsConverter {
public:
  DtDigiToStubsConverterOmtf(const OMTFConfiguration* config,
                             const OmtfAngleConverter* angleConverter,
                             edm::EDGetTokenT<L1MuDTChambPhContainer> inputTokenDtPh,
                             edm::EDGetTokenT<L1MuDTChambThContainer> inputTokenDtTh)
      : DtDigiToStubsConverter(inputTokenDtPh, inputTokenDtTh), config(config), angleConverter(angleConverter) {}

  ~DtDigiToStubsConverterOmtf() override {}

  //dtThDigis is provided as argument, because in the OMTF implementation the phi and eta digis are merged (even thought it is artificial)
  void addDTphiDigi(MuonStubPtrs2D& muonStubsInLayers,
                    const L1MuDTChambPhDigi& digi,
                    const L1MuDTChambThContainer* dtThDigis,
                    unsigned int iProcessor,
                    l1t::tftype procTyp) override;

  void addDTetaStubs(MuonStubPtrs2D& muonStubsInLayers,
                     const L1MuDTChambThDigi& thetaDigi,
                     unsigned int iProcessor,
                     l1t::tftype procTyp) override;

  bool acceptDigi(const DTChamberId& dTChamberId, unsigned int iProcessor, l1t::tftype procType) override;

private:
  const OMTFConfiguration* config = nullptr;
  const OmtfAngleConverter* angleConverter = nullptr;
};

class CscDigiToStubsConverterOmtf : public CscDigiToStubsConverter {
public:
  CscDigiToStubsConverterOmtf(const OMTFConfiguration* config,
                              const OmtfAngleConverter* angleConverter,
                              edm::EDGetTokenT<CSCCorrelatedLCTDigiCollection> inputTokenCsc)
      : CscDigiToStubsConverter(config, inputTokenCsc), config(config), angleConverter(angleConverter) {}

  ~CscDigiToStubsConverterOmtf() override {}

  //can add both phi and eta stubs
  void addCSCstubs(MuonStubPtrs2D& muonStubsInLayers,
                   unsigned int rawid,
                   const CSCCorrelatedLCTDigi& digi,
                   unsigned int iProcessor,
                   l1t::tftype procTyp) override;

  bool acceptDigi(const CSCDetId& cscDetId, unsigned int iProcessor, l1t::tftype procType) override;

  CscConversionInfo getCscConversionInfo(unsigned int rawid,
                                         const CSCCorrelatedLCTDigi& digi,
                                         unsigned int iProcessor,
                                         l1t::tftype procTyp) override;

private:
  const OMTFConfiguration* config = nullptr;
  const OmtfAngleConverter* angleConverter = nullptr;
};

class RpcDigiToStubsConverterOmtf : public RpcDigiToStubsConverter {
public:
  RpcDigiToStubsConverterOmtf(const OMTFConfiguration* config,
                              const OmtfAngleConverter* angleConverter,
                              const RpcClusterization* rpcClusterization,
                              edm::EDGetTokenT<RPCDigiCollection> inputTokenRpc,
                              bool dumpRPCDigis = true)
      : RpcDigiToStubsConverter(config, inputTokenRpc, rpcClusterization, dumpRPCDigis),
        config(config),
        angleConverter(angleConverter) {}

  ~RpcDigiToStubsConverterOmtf() override {}

  void addRPCstub(MuonStubPtrs2D& muonStubsInLayers,
                  const RPCDetId& roll,
                  const RpcCluster& cluster,
                  unsigned int iProcessor,
                  l1t::tftype procTyp) override;

  bool acceptDigi(const RPCDetId& rpcDetId, unsigned int iProcessor, l1t::tftype procType) override;

private:
  const OMTFConfiguration* config = nullptr;
  const OmtfAngleConverter* angleConverter = nullptr;
};

class OMTFinputMaker : public MuonStubMakerBase {
public:
  OMTFinputMaker(const edm::ParameterSet& edmParameterSet,
                 MuStubsInputTokens& muStubsInputTokens,
                 const OMTFConfiguration* config,
                 std::unique_ptr<OmtfAngleConverter> angleConverter);

  ~OMTFinputMaker() override;

  void initialize(const edm::ParameterSet& edmCfg,
                  const edm::EventSetup& es,
                  const MuonGeometryTokens& muonGeometryTokens) override;

  ///iProcessor - from 0 to 5
  ///returns the global phi in hardware scale (myOmtfConfig->nPhiBins() ) at which the scale starts for give processor
  static int getProcessorPhiZero(const OMTFConfiguration* config, unsigned int iProcessor);

  static void addStub(const OMTFConfiguration* config,
                      MuonStubPtrs2D& muonStubsInLayers,
                      unsigned int iLayer,
                      unsigned int iInput,
                      MuonStub& stub);

  ///Give input number for given processor, using
  ///the chamber sector number.
  ///Result is modulo allowed number of hits per chamber
  static unsigned int getInputNumber(const OMTFConfiguration* config,
                                     unsigned int rawId,
                                     unsigned int iProcessor,
                                     l1t::tftype type);

  ///Reverse mapping: given layer, input number, processor and type, 
  ///return the detector chamber information (rawId and chamber details)
  ///NOTE: This function provides the most common stub type for each detector:
  ///- DT: DT_PHI_ETA (as used in OMTF Phase1)
  ///- CSC: CSC_PHI_ETA (as used in OMTF Phase1) 
  ///- RPC: RPC
  ///For exact stub type determination, the original MuonStub object should be consulted
  struct DetectorInfo {
    unsigned int rawId = 0;
    int sector = -1;
    int station = -1; 
    int wheel = -1;    // For DT
    int endcap = -1;   // For CSC
    int ring = -1;     // For CSC/RPC
    int chamber = -1;
    int layer = -1;    // For CSC
    int roll = -1;     // For RPC
    int subsector = -1; // For RPC
    MuonStub::Type detectorType = MuonStub::EMPTY;
    std::string detectorName = "";
    std::string hwName = "";
  };

  static DetectorInfo getDetectorInfo(const OMTFConfiguration* config,
                                      unsigned int iLayer,
                                      unsigned int iInput,
                                      unsigned int iProcessor,
                                      l1t::tftype type);

  //is in the OMTFinputMakerand not in the DtDigiToStubsConverterOmtf because is also used for the phase2 DT stubs
  //it is here, because this function is needed both for the DtDigiToStubsConverterOmtf and DtPhase2DigiToStubsConverter
  static bool acceptDtDigi(const OMTFConfiguration* config,
                           const DTChamberId& dTChamberId,
                           unsigned int iProcessor,
                           l1t::tftype procType);

protected:
  const OMTFConfiguration* config = nullptr;
  std::unique_ptr<OmtfAngleConverter> angleConverter;
};

#endif
