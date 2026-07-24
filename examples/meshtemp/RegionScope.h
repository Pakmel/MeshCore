#pragma once

#include <stdint.h>
#include <helpers/TransportKeyStore.h>

// Transport-code region scoping for outgoing flood/zero-hop packets (see
// CLAUDE.md "Radio" and MESHTEMP_REGION in meshtemp_config.h). Repeaters
// that gate flood forwarding by region (RegionMap::findMatch(), called from
// filterRecvFloodPacket() in examples/simple_repeater/MyMesh.cpp) only
// relay a flood packet whose transport code matches a region they have
// explicitly allowed - an unconfigured or default-denied region silently
// drops the packet instead of forwarding it.
//
// This is intentionally independent of SensorMesh's own region_map/
// default_scope members (a CLI/filesystem-persisted mechanism meant for
// operator-configurable repeaters) - MESHTEMP_REGION stays a compile-time
// constant per CLAUDE.md's "No remote administration" rule, unreachable
// from the serial `region` commands.
class RegionScope {
public:
  // Derives the region key once from MESHTEMP_REGION. Call from setup(),
  // before the first packet is sent. No-op if MESHTEMP_REGION is empty.
  static void begin();

  // True if MESHTEMP_REGION is non-empty, i.e. whether packets should be
  // scoped at all. When false, send plain (unscoped) instead of calling
  // codesFor().
  static bool active();

  // Fills codes[0] with this region's transport code for `pkt`
  // (TransportKey::calcTransportCode() - a keyed hash of the packet's
  // payload type + payload, see src/helpers/TransportKeyStore.cpp),
  // codes[1] = 0 (no return/home region - mirrors simple_repeater's
  // MyMesh::sendFloodScoped()). Only valid to call when active() is true.
  static void codesFor(const mesh::Packet* pkt, uint16_t codes[2]);
};
