#include "ownership.h"

bool ownershipClaimsAreValid(const OwnershipClaim* claims, uint8_t count) {
  if (claims == nullptr || count == 0) return false;
  for (uint8_t left = 0; left < count; ++left) {
    if (claims[left].ownerTask == 0) return false;
    for (uint8_t right = static_cast<uint8_t>(left + 1U); right < count; ++right) {
      if (claims[left].resource == claims[right].resource) return false;
    }
  }
  return true;
}
