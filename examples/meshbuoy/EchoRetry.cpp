#include "EchoRetry.h"
#include <Arduino.h>
#include <string.h>

void EchoRetry::arm(const uint8_t* hash, unsigned long window_ms) {
  memcpy(_hash, hash, MAX_HASH_SIZE);
  _deadline = millis() + window_ms;
  _echo_heard = false;
  _armed = true;
}

void EchoRetry::onPacketSeen(const uint8_t* candidate_hash) {
  if (!_armed || _echo_heard) return;
  if (memcmp(_hash, candidate_hash, MAX_HASH_SIZE) == 0) {
    _echo_heard = true;
  }
}

bool EchoRetry::windowExpired() const {
  return _armed && (long)(millis() - _deadline) >= 0;
}
