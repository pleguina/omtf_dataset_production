/*
 * XmlIOCache.h
 *
 * Centralized cache for XML I/O data during OMTF event processing.
 * This class stores digis, stubs, and reference hits for each processor,
 * allowing consolidated XML output from a single location (OMTFProcessor).
 *
 * Design: Pass-through (non-singleton) for thread safety in CMSSW framework.
 */

#ifndef L1T_OmtfP1_XMLIOCACHE_H_
#define L1T_OmtfP1_XMLIOCACHE_H_

#include "L1Trigger/L1TMuonOverlapPhase1/interface/MuonStub.h"

#include <boost/property_tree/ptree.hpp>
#include <map>
#include <set>
#include <vector>
#include <optional>

namespace omtf {

// StubKey uniquely identifies a stub by (detId, logicLayer, input, bx)
struct StubKey {
  uint32_t detId;
  int logicLayer;
  int input;
  int bx;

  bool operator<(const StubKey& other) const {
    if (detId != other.detId)
      return detId < other.detId;
    if (logicLayer != other.logicLayer)
      return logicLayer < other.logicLayer;
    if (input != other.input)
      return input < other.input;
    return bx < other.bx;
  }

  bool operator==(const StubKey& other) const {
    return detId == other.detId && logicLayer == other.logicLayer && input == other.input && bx == other.bx;
  }
};

// Helper function to create StubKey from MuonStub
inline StubKey makeStubKey(const MuonStub& stub) {
  return {static_cast<uint32_t>(stub.detId), static_cast<int>(stub.logicLayer), static_cast<int>(stub.input),
          static_cast<int>(stub.bx)};
}

// Records for different data types
struct DigiRecord {
  std::string type;  // "CSC", "DT", "RPC"
  boost::property_tree::ptree attrs;
};

struct StubRecord {
  std::string type;  // "CSC", "DT", "RPC"
  StubKey key;
  boost::property_tree::ptree attrs;
};

struct RestrictedStubRecord {
  boost::property_tree::ptree attrs;
  int extrapolatedPhi;
};

struct ReferenceHitRecord {
  boost::property_tree::ptree attrs;
  std::vector<RestrictedStubRecord> restrictedStubs;
  std::vector<boost::property_tree::ptree> extrapCalcs;  // Calculation details for each target layer
};

// Per-processor data bucket
struct ProcessorBucket {
  std::vector<DigiRecord> digis;
  std::vector<StubRecord> stubs;
  std::set<StubKey> referenceKeys;
  std::vector<ReferenceHitRecord> refHits;

  void clear() {
    digis.clear();
    stubs.clear();
    referenceKeys.clear();
    refHits.clear();
  }
};

}  // namespace omtf

/**
 * XmlIOCache - Centralized storage for XML output data
 *
 * This class is designed to be passed through method calls (not a singleton)
 * for thread safety in CMSSW's multi-threaded framework.
 *
 * Usage:
 *   XmlIOCache cache;
 *   stubMaker->buildInputForProcessor(..., cache);
 *   processor->processInput(..., cache);
 *   cache.get(iProcessor); // Retrieve data for XML emission
 */
class XmlIOCache {
public:
  XmlIOCache() = default;
  ~XmlIOCache() = default;

  // No copy or move (we pass by reference)
  XmlIOCache(const XmlIOCache&) = delete;
  XmlIOCache& operator=(const XmlIOCache&) = delete;

  // Add a digi to the cache
  void addDigi(unsigned int iProcessor, const std::string& type, const boost::property_tree::ptree& attrs) {
    omtf::DigiRecord rec;
    rec.type = type;
    rec.attrs = attrs;
    buckets_[iProcessor].digis.push_back(rec);
  }

  // Add a stub to the cache
  void addStub(unsigned int iProcessor, const omtf::StubRecord& srec) { buckets_[iProcessor].stubs.push_back(srec); }

  // Mark a stub as a reference stub
  void markReference(unsigned int iProcessor, const omtf::StubKey& key) {
    buckets_[iProcessor].referenceKeys.insert(key);
  }

  // Add a reference hit with its restricted stubs and extrapolation calculations
  void addReferenceHit(unsigned int iProcessor, const omtf::ReferenceHitRecord& rh) {
    buckets_[iProcessor].refHits.push_back(rh);
  }

  // Add an extrapolation calculation to a specific reference hit
  void addExtrapolationCalc(unsigned int iProcessor, unsigned int iRefHit, const boost::property_tree::ptree& calc) {
    auto& bucket = buckets_[iProcessor];
    if (iRefHit < bucket.refHits.size()) {
      bucket.refHits[iRefHit].extrapCalcs.push_back(calc);
    }
  }

  // Get the bucket for a processor (returns nullptr if not found)
  const omtf::ProcessorBucket* get(unsigned int iProcessor) const {
    auto it = buckets_.find(iProcessor);
    if (it != buckets_.end())
      return &(it->second);
    return nullptr;
  }

  // Clear data for a specific processor
  void clearProcessor(unsigned int iProcessor) {
    auto it = buckets_.find(iProcessor);
    if (it != buckets_.end()) {
      it->second.clear();
    }
  }

  // Clear all processors
  void clearAll() { buckets_.clear(); }

private:
  std::map<unsigned int, omtf::ProcessorBucket> buckets_;
};

#endif /* L1T_OmtfP1_XMLIOCACHE_H_ */
