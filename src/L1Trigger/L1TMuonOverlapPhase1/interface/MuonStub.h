/*
 * MuonStub.h
 *
 *  Created on: Dec 21, 2018
 *      Author: kbunkow
 *
 *      MuonStub - data structure for algorithm input
 */

#ifndef L1T_OmtfP1_MUONSTUB_H_
#define L1T_OmtfP1_MUONSTUB_H_

#include <vector>
#include <memory>

struct MuonStub {
public:
  enum Type {
    EMPTY,
    DT_PHI,
    DT_THETA,
    DT_PHI_ETA,
    DT_HIT,
    RPC,
    RPC_DROPPED,  //to mark that all clusters were dropped because there are more than 2 clusters or at least one too big cluster
    CSC_PHI,
    CSC_ETA,
    CSC_PHI_ETA,
    BARREL_SUPER_SEG,
  };

  MuonStub();

  MuonStub(int phiHw, int phiBHw) : phiHw(phiHw), phiBHw(phiBHw) {}

  virtual ~MuonStub();

  Type type = EMPTY;

  int phiHw = 0;
  int phiBHw = 0;

  static const int EMTPY_PHI = 0xffffff;

  int etaHw = 0;
  int r = 0;  //[cm] distance from beam pipe
  int qualityHw = 0;

  int bx = 0;
  int timing = 0;

  //used to address LUTs
  unsigned int logicLayer = 0;

  unsigned int input = 0;

  //int roll = 0;  //TODO remove

  int detId = 0;

  // Raw, pre-processor-local disambiguator captured directly from the source
  // digi/cluster BEFORE any processor-specific windowing/formatting (phiHw,
  // input) is applied. Unlike 'input' (a processor-local channel index) and
  // 'phiHw' (processor-local phi, see getProcessorPhi), this value is the same
  // for the same physical primitive regardless of which processor(s) consume
  // it (e.g. in the geometrical overlap region between two adjacent OMTF
  // processors). Populated in OMTFinputMaker.cc:
  //   DT:  raw local phi count digi.phi() (chamber-frame, pre-conversion)
  //   CSC: pack(keyWG, halfStrip) from the raw LCT digi
  //   RPC: pack(firstStrip, lastStrip) from the raw cluster
  // Used as the "sourceIndex" component of the stable per-primitive ID
  // (see DataROOTDumper2AllInput::makePrimitiveId, primitiveIdVersion=2).
  int sourceIndex = 0;

  // Stable per-primitive ID (primitiveIdVersion=2, see interface/OmtfPrimitiveId.h),
  // computed once in OMTFinputMaker::addStub() after all fields above (type, detId,
  // bx, sourceIndex) are populated. Processor-invariant: the same physical primitive
  // carries the same primitiveId regardless of which processor(s) consume it.
  unsigned long long primitiveId = 0;

  // CSC conversion parameters (only valid for CSC stubs)
  int cscOffset = 0;     // fixOff from angle conversion
  double cscScale = 0;   // scale from angle conversion  
  int cscOrder = 0;      // order from angle conversion

  friend std::ostream& operator<<(std::ostream& out, const MuonStub& stub);
};

typedef std::vector<MuonStub> MuonStubs1D;
typedef std::vector<MuonStubs1D> MuonStubs2D;

typedef std::shared_ptr<MuonStub> MuonStubPtr;
typedef std::vector<MuonStubPtr> MuonStubPtrs1D;
typedef std::vector<MuonStubPtrs1D> MuonStubPtrs2D;

#endif /* L1T_OmtfP1_MUONSTUB_H_ */
