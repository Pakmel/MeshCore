#include "RegionScope.h"
#include "pooltemp_config.h"
#include <Utils.h>
#include <string.h>

static TransportKey region_key;

bool RegionScope::active() {
  return POOLTEMP_REGION[0] != 0;
}

void RegionScope::begin() {
  if (!active()) return;

  // Mirrors RegionMap::getTransportKeysFor()'s "implicit auto hashtag
  // region" path (src/helpers/RegionMap.cpp): the key material is SHA256 of
  // the region name with a '#' prepended - the same convention a repeater
  // operator gets from `region put se1780` (with or without a leading '#' in
  // the name, both resolve to this same key), so nothing extra needs
  // configuring on the repeater's TransportKeyStore side beyond the region
  // entry itself.
  char name[sizeof(POOLTEMP_REGION) + 1];
  name[0] = '#';
  strcpy(&name[1], POOLTEMP_REGION);

  mesh::Utils::sha256(region_key.key, sizeof(region_key.key), (const uint8_t*)name, strlen(name));
}

void RegionScope::codesFor(const mesh::Packet* pkt, uint16_t codes[2]) {
  codes[0] = region_key.calcTransportCode(pkt);
  codes[1] = 0;
}
