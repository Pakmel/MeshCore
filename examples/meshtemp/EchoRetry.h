#pragma once

#include <stdint.h>
#include <MeshCore.h>

// Tracks whether a repeater has bounced our own just-sent packet back to
// us within a time window, timed by millis() (never loop iterations).
// Pure state holder - matching incoming packet hashes against the armed
// hash happens via onPacketSeen(), fed from a Dispatcher::logRx() override
// elsewhere, since that's the earliest point a raw incoming packet can be
// inspected before MeshCore's own flood-dedup (hasSeen()) would otherwise
// swallow the echo of our own transmission as a "duplicate".
class EchoRetry {
public:
  // Starts watching for `hash` (MAX_HASH_SIZE bytes) for `window_ms`
  // milliseconds from now.
  void arm(const uint8_t* hash, unsigned long window_ms);

  void disarm() { _armed = false; }

  bool armed() const { return _armed; }

  // Call for every received packet's hash. No-op unless armed().
  void onPacketSeen(const uint8_t* candidate_hash);

  bool echoHeard() const { return _echo_heard; }

  // True once the window has elapsed, per millis() - never call this to
  // decide whether to read the echo result before checking armed().
  bool windowExpired() const;

private:
  bool _armed = false;
  bool _echo_heard = false;
  uint8_t _hash[MAX_HASH_SIZE];
  unsigned long _deadline = 0;
};
