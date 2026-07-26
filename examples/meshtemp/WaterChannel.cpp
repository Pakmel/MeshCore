#include "WaterChannel.h"
#include "meshtemp_config.h"
#include <Utils.h>
#include <string.h>
#include <stdio.h>

void WaterChannel::begin() {
  memset(channel.secret, 0, sizeof(channel.secret));
  mesh::Utils::sha256(channel.secret, CIPHER_KEY_SIZE,
                       (const uint8_t*)MESHTEMP_CHANNEL_NAME, strlen(MESHTEMP_CHANNEL_NAME));
  // 1-byte routing hash, derived the same way BaseChatMesh::setChannel does
  // for a 128-bit (16-byte) secret: sha256 of just the key bytes, not the
  // full zero-padded 32-byte secret buffer.
  mesh::Utils::sha256(channel.hash, sizeof(channel.hash), channel.secret, CIPHER_KEY_SIZE);
}

bool WaterChannel::selfCheckKeyDerivation() {
  static const uint8_t expected[CIPHER_KEY_SIZE] = {
    0x9c, 0xd8, 0xfc, 0xf2, 0x2a, 0x47, 0x33, 0x3b,
    0x59, 0x1d, 0x96, 0xa2, 0xb8, 0x48, 0xb7, 0x3f
  };
  const char* test_name = "#test";

  uint8_t key[CIPHER_KEY_SIZE];
  mesh::Utils::sha256(key, sizeof(key), (const uint8_t*)test_name, strlen(test_name));

  bool pass = memcmp(key, expected, CIPHER_KEY_SIZE) == 0;

  char hex[CIPHER_KEY_SIZE * 2 + 1];
  mesh::Utils::toHex(hex, key, sizeof(key));

  Serial.print("[channel key self-check] #test -> ");
  Serial.print(hex);
  Serial.println(pass ? "  PASS" : "  FAIL");

  return pass;
}

void WaterChannel::formatMessage(char* out, size_t out_size, const WaterReading& reading, float batt_v) {
  switch (reading.case_type) {
    // The leading word before the first ": " is what MeshCore clients display
    // as the sender name for a channel message (see docs/payloads.md - channel
    // messages carry no identity of their own, so the convention is
    // "<sender name>: <message body>"). It is therefore a user-visible label,
    // not decoration: this node shows up as "Temperature" in the app.
    case WaterReadingCase::NORMAL:
      snprintf(out, out_size, "Temperature: %.1fC Batt: %.2fV", reading.temp_c, batt_v);
      break;
    case WaterReadingCase::IMPLAUSIBLE:
      snprintf(out, out_size, "Temperature: %.1fC? Batt: %.2fV", reading.temp_c, batt_v);
      break;
    case WaterReadingCase::SENSOR_ERROR:
    default:
      snprintf(out, out_size, "Temperature: ERR(%d) Batt: %.2fV", reading.error_code, batt_v);
      break;
  }
}
