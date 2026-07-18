#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Mesh.h>
#include "WaterTempSensor.h"

#define MESHTEMP_MSG_MAX_LEN 48

// Hashtag-channel key derivation and message formatting for MeshTemp.
// See CLAUDE.md "Radio" (channel key derivation) and "Message format"
// (the 3-case contract) - this class is the single place that contract
// gets turned into bytes, so it can't drift between test-mode logging and
// the real channel send.
class WaterChannel {
public:
  mesh::GroupChannel channel;

  // Derives channel.secret (first CIPHER_KEY_SIZE bytes = SHA256 of
  // MESHTEMP_CHANNEL_NAME, remainder zeroed) and channel.hash (1-byte
  // routing hash = SHA256 of that key), mirroring
  // BaseChatMesh::setChannel's 128-bit-key path exactly.
  void begin();

  // Runs the known-answer check from CLAUDE.md: "#test" must derive the
  // key 9cd8fcf22a47333b591d96a2b848b73f. Logs PASS/FAIL to serial.
  // Returns true on pass. Does not touch `channel` - independent check.
  static bool selfCheckKeyDerivation();

  // Formats the exact contract string for one of the 3 message cases.
  // `out` must be at least MESHTEMP_MSG_MAX_LEN bytes.
  static void formatMessage(char* out, size_t out_size, const WaterReading& reading, float batt_v);
};
