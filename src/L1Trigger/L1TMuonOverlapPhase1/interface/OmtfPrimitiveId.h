#ifndef L1T_OmtfP1_OMTFPRIMITIVEID_H_
#define L1T_OmtfP1_OMTFPRIMITIVEID_H_

#include <cstdint>

// Shared primitiveId v2 recipe (see MuonStub::sourceIndex and
// DataROOTDumper2AllInput.h kPrimitiveIdVersion for the full rationale).
// Used both at stub-construction time (OMTFinputMaker.cc, to populate
// MuonStub::primitiveId once) and by the ROOT dumpers that persist it, so the
// hash formula lives in exactly one place instead of being duplicated.
//
// v2 hashes ONLY source-identity fields that are invariant across processor
// duplication: sourceCollection (== stub type/technology), sourceDetId (==
// raw source chamber/roll DetId), sourceIndex (raw pre-conversion
// disambiguator, see MuonStub::sourceIndex), sourceBx (source BX). No truth
// information, no floating-point fields, deterministic across jobs/platforms.
//
// v1 (deprecated, do not use for new productions) additionally hashed the
// processor-local 'input' channel index and the processor-local 'phiHw'
// value, so the SAME physical primitive copied into two overlapping
// processors received TWO DIFFERENT primitiveId values.
namespace omtf {

  constexpr int kPrimitiveIdVersion = 2;

  inline uint64_t fnv1a64Update(uint64_t h, uint64_t x) {
    constexpr uint64_t kPrime = 1099511628211ULL;
    for (unsigned int i = 0; i < 8; ++i) {
      h ^= static_cast<uint8_t>((x >> (8 * i)) & 0xFFULL);
      h *= kPrime;
    }
    return h;
  }

  inline uint64_t makePrimitiveId(signed char sourceCollection,
                                  uint32_t sourceDetId,
                                  int sourceIndex,
                                  signed char sourceBx) {
    uint64_t h = 1469598103934665603ULL;
    h = fnv1a64Update(h, static_cast<uint64_t>(static_cast<uint8_t>(sourceCollection)));
    h = fnv1a64Update(h, static_cast<uint64_t>(sourceDetId));
    h = fnv1a64Update(h, static_cast<uint64_t>(static_cast<uint32_t>(sourceIndex)));
    h = fnv1a64Update(h, static_cast<uint64_t>(static_cast<uint8_t>(sourceBx)));
    return h;
  }

}  // namespace omtf

#endif /* L1T_OmtfP1_OMTFPRIMITIVEID_H_ */
