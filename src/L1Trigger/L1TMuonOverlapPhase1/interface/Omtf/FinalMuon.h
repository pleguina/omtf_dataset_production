/*
 * FinalMuon.h
 *
 *  Created on: Dec 17, 2024
 *      Author: kbunkow
 */

#ifndef L1T_OmtfP1_FinalMuon_H
#define L1T_OmtfP1_FinalMuon_H

#include "L1Trigger/L1TMuonOverlapPhase1/interface/Omtf/AlgoMuon.h"

class FinalMuon {
public:
  FinalMuon() {};
  FinalMuon(AlgoMuonPtr algoMuon) : algoMuon(algoMuon) {};

  virtual ~FinalMuon() {};

  const AlgoMuonPtr& getAlgoMuon() const { return algoMuon; }

  int getEta() const { return eta; }

  void setEta(int eta = 0) { this->eta = eta; }

  int getHwD0() const { return hwD0; }

  void setHwD0(int hwD0) { this->hwD0 = hwD0; }

  int getPhi() const { return phi; }

  void setPhi(int phi = 0) { this->phi = phi; }

  int getPt() const { return pt; }

  void setPt(int pt = 0) { this->pt = pt; }

  int getPtUnconstr() const { return ptUnconstr; }

  void setPtUnconstr(int ptUnconstr = 0) { this->ptUnconstr = ptUnconstr; }

  int getQuality() const { return quality; }

  void setQuality(int quality = 0) { this->quality = quality; }

  int getSign() const { return sign; }

  void setSign(int sign = 0) { this->sign = sign; }

private:
  AlgoMuonPtr algoMuon;
  int pt = 0;
  int ptUnconstr = 0;
  int phi = 0;
  int eta = 0;
  int sign = 0;
  int quality = 0;
  int hwD0 = 0;  //displacement i.e. dxy
};

typedef std::vector<FinalMuon> FinalMuons;

#endif /* FinalMuon */
