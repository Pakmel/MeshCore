#pragma once

#include <stdint.h>
#include <Mesh.h>
#include <helpers/ClientACL.h>
#include "SensorMesh.h"

// Discovery-driven RTC sync, cold boot first, then piggybacked retry.
//
// Cold boot: broadcast one zero-hop node-discovery request
// (CTL_TYPE_NODE_DISCOVER_REQ, filter=repeater), take the first repeater
// that answers, then send it a MeshCore ANON_REQ_TYPE_BASIC "remote clock"
// request and apply the reply forward-only. Both request types are answered
// unauthenticated by repeater firmware - confirmed by reading the code, not
// assumed: MyMesh::onControlDataRecv's CTL_TYPE_NODE_DISCOVER_REQ branch is
// gated only by `!_prefs.disable_fwd` and a rate limiter
// (`discover_limiter`), and MyMesh::handleAnonClockReq is gated only by
// `anon_limiter` - neither checks a password or ACL entry
// (examples/simple_repeater/MyMesh.cpp).
//
// If that fails or times out within the boot window, sending is NEVER
// blocked on it - meshtempSendCurrentReadingAndArm() sends immediately
// either way, with timestamp 0 if still unsynced (see main.cpp). The next
// regular wake cycle tries again (retryIfNeeded()), piggybacked on the
// cycle that's happening anyway - no dedicated wakeup - until the first
// successful sync, then never again.
//
// The passive advert-based sync (main.cpp's onAdvertRecv override) is a
// second, independent path to the same result: hearing any repeater's
// advert (zero-hop or otherwise) also supplies a usable timestamp. Both
// paths funnel through applyIfNewer() so "did we actually sync, and from
// what" has one answer and one log format regardless of which path got
// there first.
//
// Independent of the echo-retry mechanism: uses onControlDataRecv() and
// onPeerDataRecv(), both post-dedup callbacks on the normal Dispatcher
// pipeline, not the pre-dedup logRx() hook EchoRetry needs for detecting
// its own echoed packet. Nothing here touches EchoRetry/logRx().
class BootTimeSync {
public:
  // True once a repeater's (or advert's) clock has been successfully
  // applied at least once since cold boot. Retries stop once this is true.
  bool synced() const { return _synced; }

  // Applies `new_time` forward-only (mirrors CommonCLI's "clock sync"
  // command, src/helpers/CommonCLI.cpp) and logs the transition. Called
  // from both the active (this class) and passive (main.cpp onAdvertRecv)
  // paths, so both log identically and agree on synced() afterwards.
  // Returns true if the clock was actually moved.
  bool applyIfNewer(SensorMesh& mesh, uint32_t new_time, const char* source_label);

  // Call at the start of every cycle (the first one at boot, and every
  // subsequent wake). No-op once synced(), and no-op if an attempt from a
  // previous call is still in flight (never stacks). Never blocks the
  // caller - fires the discovery broadcast and returns immediately.
  void retryIfNeeded(SensorMesh& mesh);

  // Call every loop() iteration. Only handles timeouts of the in-flight
  // attempt (if any) - never blocks, never delays sleep.
  void tick(SensorMesh& mesh);

  // Feed every received PAYLOAD_TYPE_CONTROL packet here (from
  // onControlDataRecv). No-op unless it's a matching discover response and
  // we're currently waiting for one.
  void onControlData(SensorMesh& mesh, const mesh::Packet* pkt);

  // Feed every received PAYLOAD_TYPE_RESPONSE peer datagram here (from
  // onPeerDataRecv). No-op unless it's from the repeater we're waiting on,
  // with a matching tag.
  void onPeerResponse(SensorMesh& mesh, const ClientInfo* from, const uint8_t* data, size_t len);

private:
  enum class State : uint8_t { IDLE, WAITING_FOR_DISCOVERY, WAITING_FOR_CLOCK };

  State _state = State::IDLE;
  bool _synced = false;
  unsigned long _deadline = 0;
  uint32_t _discover_tag = 0;
  uint32_t _clock_req_tag = 0;
  mesh::Identity _repeater_id;

  void sendClockRequest(SensorMesh& mesh);
};
