#pragma once

#include <stdint.h>
#include <Mesh.h>
#include <helpers/ClientACL.h>
#include "SensorMesh.h"

// Two-phase, source-skeptical RTC sync.
//
// PHASE 1 - first sync (cold boot, clock never yet trusted):
// Both the active discovery+clock-request mechanism below AND the passive
// advert path (main.cpp's onAdvertRecv override) feed candidate timestamps
// into this class via proposeTime() - neither can seed the clock alone from
// a single unverified source. Up to 2 distinct repeaters are tracked at
// once (mind the repeater's own anon-request rate limit - "four per 180s",
// examples/simple_repeater/MyMesh.cpp:855 - two requests per attempt stays
// well under that):
//   - 2 sources agree (within TIME_AGREEMENT_WINDOW_SECS of each other):
//     apply the EARLIER of the two, unrestricted direction (no clock has
//     been trusted yet, so "forward-only" doesn't apply here at all).
//   - 2 sources disagree beyond that window: trust neither, log both by
//     name, discard, retry next cycle.
//   - Only 1 source ever heard, after the boot attempt plus one full retry
//     cycle: accept it alone (logged as "single-source") rather than wait
//     forever for a second corroborating source that may not exist.
//   - 0 sources: keep retrying every cycle, no cap.
//
// PHASE 2 - once synced(): every subsequently proposed time (still via the
// same proposeTime(), from either path, or from a repeater whose earlier
// reply finally arrives late) goes through a symmetric sanity check instead
// of the source-count ladder above: applied immediately, forward OR
// backward, if it's within TIME_SANITY_MAX_JUMP_SECS of the current clock;
// rejected and logged (naming the source) if the jump is larger in either
// direction. This is deliberately not "trust the first thing that shows up
// again" - a single bad repeater can still nudge an already-good clock, but
// only within a bounded, plausible-drift-sized window.
//
// Both request types this class sends (CTL_TYPE_NODE_DISCOVER_REQ,
// ANON_REQ_TYPE_BASIC) are answered unauthenticated by repeater firmware -
// confirmed by reading examples/simple_repeater/MyMesh.cpp (gated only by
// rate limiters, no password) - which is exactly why a lone repeater's
// answer is never trusted alone on first sync: anyone can stand up a
// repeater and answer these.
//
// Independent of the echo-retry mechanism: uses onControlDataRecv() and
// onPeerDataRecv(), both post-dedup callbacks on the normal Dispatcher
// pipeline, not the pre-dedup logRx() hook EchoRetry needs for detecting
// its own echoed packet. Nothing here touches EchoRetry/logRx().
class BootTimeSync {
public:
  // True once the clock has been trusted at least once (2-source agreement
  // or the single-source fallback). Governs which phase proposeTime() uses,
  // and BootTimeSync's own retryIfNeeded() stops issuing new discovery
  // broadcasts once true (the passive advert path keeps running forever,
  // now under phase 2's symmetric sanity check).
  bool synced() const { return _synced; }

  // The one entry point for "a source claims the time is X" - called from
  // both the active clock-request reply handler below and main.cpp's
  // passive onAdvertRecv override. See the phase 1 / phase 2 description
  // above for what happens. `source_id` is used to de-duplicate/identify a
  // candidate; `source_label` is for logging only (a name if we have one,
  // otherwise a short pubkey-hex identifier).
  void proposeTime(SensorMesh& mesh, const mesh::Identity& source_id, uint32_t new_time, const char* source_label);

  // Call at the start of every cycle. No-op once synced() (the active
  // mechanism's job is done; ongoing correction is the passive path's,
  // per phase 2 above) and no-op if an attempt from a previous call is
  // still in flight (never stacks). Never blocks the caller - fires
  // whatever's needed and returns immediately.
  void retryIfNeeded(SensorMesh& mesh);

  // Call every loop() iteration. Only handles timeouts of the in-flight
  // attempt (if any) - never blocks, never delays sleep.
  void tick(SensorMesh& mesh);

  // Feed every received PAYLOAD_TYPE_CONTROL packet here (from
  // onControlDataRecv). No-op unless it's a matching discover response
  // during an active discovery window with a free candidate slot.
  void onControlData(SensorMesh& mesh, const mesh::Packet* pkt);

  // Feed every received PAYLOAD_TYPE_RESPONSE peer datagram here (from
  // onPeerDataRecv). No-op unless it matches a candidate slot we sent a
  // clock request for and are still waiting on.
  void onPeerResponse(SensorMesh& mesh, const ClientInfo* from, const uint8_t* data, size_t len);

private:
  enum class State : uint8_t { IDLE, WAITING_FOR_DISCOVERY, WAITING_FOR_CLOCK };

  struct Candidate {
    bool have_id = false;
    mesh::Identity id;
    char label[40] = {0};
    bool requested = false;      // an active clock-request is/was in flight for this slot
    bool have_response = false;  // true once timestamp is valid (active reply or passive advert)
    uint32_t timestamp = 0;
    uint32_t clock_req_tag = 0;
  };

  State _state = State::IDLE;
  bool _synced = false;
  unsigned long _deadline = 0;
  uint32_t _discover_tag = 0;
  int _sync_attempts = 0;   // count of *completed* active attempts, for the single-source fallback

  Candidate _candidates[2];

  int findOrAddCandidateSlot(const mesh::Identity& id);
  int countValidCandidates() const;
  void sendClockRequestForSlot(SensorMesh& mesh, int slot);
  void evaluateTwoIfReady(SensorMesh& mesh);
  void finishAttempt(SensorMesh& mesh);
  void resetCandidates();
};
