#pragma once

#include <stdint.h>
#include <Mesh.h>
#include <helpers/ClientACL.h>
#include "SensorMesh.h"
#include "meshtemp_config.h"   // MAX_TIME_SYNC_CANDIDATES - needed for the array sizes below

// Two-phase, source-skeptical RTC sync.
//
// PHASE 1 - first sync (cold boot, clock never yet trusted):
// Both the active discovery+clock-request mechanism below AND the passive
// advert path (main.cpp's onAdvertRecv override) feed candidate timestamps
// into this class via proposeTime() - neither can seed the clock alone from
// a single unverified source. Up to MAX_TIME_SYNC_CANDIDATES distinct
// sources are tracked at once, accumulated across the boot attempt AND
// every retry cycle (mind the repeater's own anon-request rate limit -
// "four per 180s", examples/simple_repeater/MyMesh.cpp:855 - this is a
// per-repeater limit, and each distinct repeater is only ever sent one
// clock request per candidate slot, so tracking more distinct repeaters
// concurrently doesn't stress any single one's limiter):
//   - Every time a new candidate's timestamp arrives, it's checked against
//     every OTHER candidate already in the pool - not just "whoever's in
//     the other slot". If ANY pair agrees (within TIME_AGREEMENT_WINDOW_SECS
//     of each other), sync immediately: apply the EARLIER of that pair,
//     unrestricted direction (no clock has been trusted yet, so
//     "forward-only" doesn't apply here at all). Every other candidate in
//     the pool at that point is logged and ignored as an outlier - a lone
//     bad source must never block consensus between two good ones, and a
//     bad source is never "compared against" in a way that discards a good
//     one still sitting in the pool.
//   - No agreeing pair yet: keep the whole pool and keep listening/retrying
//     - nothing is discarded on a mere disagreement between two particular
//     candidates, since a third might still agree with one of them.
//   - A disagreeing pair does NOT get discarded - both candidates stay in
//     the pool (a third source might still agree with one of them), but
//     both are marked `distrusted` for the rest of this boot session. This
//     is knowledge, not a verdict: a pairwise disagreement alone can't say
//     WHICH of the two is wrong, so neither is removed from eligibility for
//     the pair-agreement path above (either could still sync normally by
//     agreeing with some third source later) - `distrusted` only ever gates
//     the single-source fallback below, nothing else.
//   - Only 1 distinct source ever heard, after the boot attempt plus one
//     full retry cycle: accept it alone (logged as "single-source") -
//     UNLESS that source has already lost a disagreement this session
//     (`distrusted`), in which case it's explicitly withheld and logged as
//     such rather than trusted. A source that's already been contradicted
//     once must not get a second, unearned chance to seed the clock alone
//     just because whoever it disagreed with didn't answer this round.
//   - 0 sources: keep retrying every cycle, no cap.
//   - Pool full (MAX_TIME_SYNC_CANDIDATES distinct sources) and a new
//     distinct source shows up: the oldest-added candidate WITHOUT an
//     active clock-request still in flight is evicted to make room (an
//     in-flight one is skipped so its eventual reply isn't silently
//     orphaned - see onPeerResponse()). Only if every tracked candidate is
//     simultaneously in-flight does eviction fall back to the oldest
//     in-flight one, logged when it happens.
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
// SELF-DISTRUST ESCALATION (phase 2, mandatory): TIME_SANITY_MAX_JUMP_SECS
// guards against any one bad proposal, but says nothing about the case
// where the trusted clock ITSELF is the wrong one (e.g. seeded from a bad
// single-source fallback) - every legitimate correction from the real world
// would otherwise keep getting rejected as "implausible" forever, with no
// way out. Every rejected proposal is recorded (by distinct source
// identity, most recent value per source). Once at least
// TIME_DISTRUST_THRESHOLD rejections have accumulated since the last
// sync/re-seed, AND at least two distinct rejecting sources agree with each
// other (within TIME_AGREEMENT_WINDOW_SECS), the node concludes ITS OWN
// clock is the outlier: it drops the current time and re-seeds from that
// agreeing pair in one motion (never observably unsynced in between -
// _synced stays true throughout, so retryIfNeeded()'s active ladder does
// not resume), fully logged. Requires both the count AND real corroboration
// - a single persistently-wrong source spamming rejections can't trigger
// this alone.
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
    bool distrusted = false;     // phase 1 only: lost a pairwise disagreement this boot session - see header comment
    unsigned long added_at = 0;  // millis() this slot was (re)claimed, for oldest-first eviction
  };

  State _state = State::IDLE;
  bool _synced = false;
  unsigned long _deadline = 0;
  uint32_t _discover_tag = 0;
  int _sync_attempts = 0;   // count of *completed* active attempts, for the single-source fallback

  // Phase 1 candidate pool - accumulated across the whole boot session,
  // never wiped by a mere disagreement (only on a successful sync, which
  // ends phase 1 entirely). _num_candidates only grows (or holds steady
  // across a slot reuse/eviction) - it is not a "how many are valid right
  // now" count, see countValidCandidates() for that.
  Candidate _candidates[MAX_TIME_SYNC_CANDIDATES];
  int _num_candidates = 0;

  // Phase 2 self-distrust escalation tracking - distinct sources behind
  // recent REJECTED proposals (most recent rejected value per source), plus
  // a running count of total rejection events (not deduped by source) since
  // the last sync/re-seed. See header comment's "SELF-DISTRUST ESCALATION".
  Candidate _distrust_pool[MAX_TIME_SYNC_CANDIDATES];
  int _distrust_num = 0;
  int _distrust_reject_count = 0;

  int findOrAddCandidateSlot(const mesh::Identity& id);
  int countValidCandidates() const;
  void sendClockRequestForSlot(SensorMesh& mesh, int slot);
  void finishAttempt(SensorMesh& mesh);
  void resetCandidates();
  void checkSelfDistrustEscalation(SensorMesh& mesh, const mesh::Identity& source_id, uint32_t rejected_time, const char* source_label);
};
