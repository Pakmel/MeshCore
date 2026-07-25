#include "BootTimeSync.h"
#include "meshtemp_config.h"
#include <helpers/AdvertDataHelpers.h>
#include <Utils.h>
#include <RTClib.h>
#include <string.h>
#include <Arduino.h>

// Local to this file, matching examples/simple_repeater/MyMesh.cpp's own
// #defines for the same wire protocol - not shared/exported anywhere.
#define CTL_TYPE_NODE_DISCOVER_REQ   0x80
#define CTL_TYPE_NODE_DISCOVER_RESP  0x90
#define ANON_REQ_TYPE_BASIC          0x03   // "just remote clock" - see handleAnonClockReq

#define BOOT_SYNC_DISCOVERY_TIMEOUT_MS  (RETRY_WINDOW_S * 1000UL)
#define BOOT_SYNC_CLOCK_TIMEOUT_MS      (RETRY_WINDOW_S * 1000UL)

static void logDateTime(uint32_t epoch_secs) {
  DateTime dt(epoch_secs);
  Serial.printf("%02d:%02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.second(), dt.day(), dt.month(), dt.year());
}

int BootTimeSync::findOrAddCandidateSlot(const mesh::Identity& id) {
  for (int i = 0; i < _num_candidates; i++) {
    if (_candidates[i].have_id && _candidates[i].id.matches(id)) return i;
  }
  for (int i = 0; i < _num_candidates; i++) {
    if (!_candidates[i].have_id) {   // a previously-freed slot (timed out with no reply)
      _candidates[i] = Candidate();
      _candidates[i].have_id = true;
      _candidates[i].id = id;
      _candidates[i].added_at = millis();
      return i;
    }
  }
  if (_num_candidates < MAX_TIME_SYNC_CANDIDATES) {
    int i = _num_candidates++;
    _candidates[i] = Candidate();
    _candidates[i].have_id = true;
    _candidates[i].id = id;
    _candidates[i].added_at = millis();
    return i;
  }

  // Pool full - evict the oldest slot that does NOT have an active
  // clock-request still in flight (evicting an in-flight one would silently
  // orphan its reply when it eventually arrives - see onPeerResponse()).
  // Only fall back to evicting an in-flight slot if literally every tracked
  // candidate is in flight at once.
  int oldest = -1;
  for (int i = 0; i < _num_candidates; i++) {
    bool in_flight = _candidates[i].requested && !_candidates[i].have_response;
    if (in_flight) continue;
    if (oldest < 0 || _candidates[i].added_at < _candidates[oldest].added_at) oldest = i;
  }
  if (oldest < 0) {
    oldest = 0;
    for (int i = 1; i < _num_candidates; i++) {
      if (_candidates[i].added_at < _candidates[oldest].added_at) oldest = i;
    }
    Serial.print("[time sync] candidate pool full and all in flight - evicting oldest in-flight (");
    Serial.print(_candidates[oldest].label);
    Serial.println(")");
  }
  _candidates[oldest] = Candidate();
  _candidates[oldest].have_id = true;
  _candidates[oldest].id = id;
  _candidates[oldest].added_at = millis();
  return oldest;
}

int BootTimeSync::countValidCandidates() const {
  int n = 0;
  for (int i = 0; i < _num_candidates; i++) {
    if (_candidates[i].have_response) n++;
  }
  return n;
}

void BootTimeSync::resetCandidates() {
  for (int i = 0; i < _num_candidates; i++) {
    _candidates[i] = Candidate();
  }
  _num_candidates = 0;
}

void BootTimeSync::checkSelfDistrustEscalation(SensorMesh& mesh, const mesh::Identity& source_id, uint32_t rejected_time, const char* source_label) {
  _distrust_reject_count++;

  int slot = -1;
  for (int i = 0; i < _distrust_num; i++) {
    if (_distrust_pool[i].have_id && _distrust_pool[i].id.matches(source_id)) { slot = i; break; }
  }
  if (slot < 0) {
    if (_distrust_num < MAX_TIME_SYNC_CANDIDATES) {
      slot = _distrust_num++;
    } else {
      slot = 0;
      for (int i = 1; i < _distrust_num; i++) {
        if (_distrust_pool[i].added_at < _distrust_pool[slot].added_at) slot = i;
      }
    }
    _distrust_pool[slot] = Candidate();
    _distrust_pool[slot].have_id = true;
    _distrust_pool[slot].id = source_id;
    _distrust_pool[slot].added_at = millis();
  }
  strncpy(_distrust_pool[slot].label, source_label, sizeof(_distrust_pool[slot].label) - 1);
  _distrust_pool[slot].have_response = true;
  _distrust_pool[slot].timestamp = rejected_time;

  if (_distrust_reject_count < TIME_DISTRUST_THRESHOLD) return;

  for (int i = 0; i < _distrust_num; i++) {
    if (!_distrust_pool[i].have_response) continue;
    for (int j = i + 1; j < _distrust_num; j++) {
      if (!_distrust_pool[j].have_response) continue;

      uint32_t a = _distrust_pool[i].timestamp, b = _distrust_pool[j].timestamp;
      uint32_t diff = a > b ? a - b : b - a;
      if (diff > TIME_AGREEMENT_WINDOW_SECS) continue;

      bool i_earlier = a <= b;
      uint32_t earlier = i_earlier ? a : b;
      const char* earlier_label = i_earlier ? _distrust_pool[i].label : _distrust_pool[j].label;
      uint32_t old_time = mesh.getRTCClock()->getCurrentTime();

      Serial.print("[time sync] SELF-DISTRUST ESCALATION: ");
      Serial.print(_distrust_reject_count);
      Serial.print(" rejected proposals this session, and 2 distinct sources agree with each other (delta ");
      Serial.print(diff);
      Serial.print("s): ");
      Serial.print(_distrust_pool[i].label);
      Serial.print("=");
      logDateTime(a);
      Serial.print(", ");
      Serial.print(_distrust_pool[j].label);
      Serial.print("=");
      logDateTime(b);
      Serial.println();

      Serial.print("[time sync] dropping our own clock (");
      logDateTime(old_time);
      Serial.print(") and re-seeding from that consensus (");
      Serial.print(earlier_label);
      Serial.print("): ");
      logDateTime(earlier);
      Serial.println();

      // Applied directly, _synced never observably goes false - this is a
      // re-seed, not a return to the unsynced state (retryIfNeeded()'s
      // active discovery ladder must not resume).
      mesh.getRTCClock()->setCurrentTime(earlier);

      for (int k = 0; k < _distrust_num; k++) _distrust_pool[k] = Candidate();
      _distrust_num = 0;
      _distrust_reject_count = 0;
      return;
    }
  }
}

void BootTimeSync::proposeTime(SensorMesh& mesh, const mesh::Identity& source_id, uint32_t new_time, const char* source_label) {
  if (_synced) {
    // PHASE 2: already trusted once - symmetric sanity window, either direction.
    uint32_t curr = mesh.getRTCClock()->getCurrentTime();
    int64_t delta = (int64_t)new_time - (int64_t)curr;
    uint32_t abs_delta = (uint32_t)(delta < 0 ? -delta : delta);

    if (abs_delta <= TIME_SANITY_MAX_JUMP_SECS) {
      mesh.getRTCClock()->setCurrentTime(new_time);
      Serial.print("[time sync] adjusted from ");
      Serial.print(source_label);
      Serial.print(" (");
      Serial.print(delta >= 0 ? "+" : "-");
      Serial.print(abs_delta);
      Serial.print("s): ");
      logDateTime(curr);
      Serial.print(" -> ");
      logDateTime(new_time);
      Serial.println();
    } else {
      Serial.print("[time sync] rejected implausible jump from ");
      Serial.print(source_label);
      Serial.print(": ours=");
      logDateTime(curr);
      Serial.print(" proposed=");
      logDateTime(new_time);
      Serial.print(" (delta ");
      Serial.print(abs_delta);
      Serial.print("s > TIME_SANITY_MAX_JUMP_SECS=");
      Serial.print((uint32_t)TIME_SANITY_MAX_JUMP_SECS);
      Serial.println("s)");

      checkSelfDistrustEscalation(mesh, source_id, new_time, source_label);
    }
    return;
  }

  // PHASE 1: accumulate distinct candidates across the whole boot session
  // and search for any agreeing pair - see BootTimeSync.h for the full
  // design.
  int slot = findOrAddCandidateSlot(source_id);

  strncpy(_candidates[slot].label, source_label, sizeof(_candidates[slot].label) - 1);
  _candidates[slot].have_response = true;
  _candidates[slot].timestamp = new_time;

  Serial.print("[time sync] candidate: ");
  Serial.print(source_label);
  Serial.print(" reports ");
  logDateTime(new_time);
  Serial.println();

  // Seek agreement between this candidate and every OTHER candidate already
  // in the pool - not just "whoever's in the other slot".
  for (int i = 0; i < _num_candidates; i++) {
    if (i == slot || !_candidates[i].have_response) continue;

    uint32_t a = _candidates[slot].timestamp, b = _candidates[i].timestamp;
    uint32_t diff = a > b ? a - b : b - a;
    if (diff > TIME_AGREEMENT_WINDOW_SECS) continue;

    bool slot_earlier = a <= b;
    uint32_t earlier = slot_earlier ? a : b;
    const char* earlier_label = slot_earlier ? _candidates[slot].label : _candidates[i].label;

    mesh.getRTCClock()->setCurrentTime(earlier);
    _synced = true;

    Serial.print("[time sync] synced (2-source agreement, delta ");
    Serial.print(diff);
    Serial.print("s): ");
    Serial.print(_candidates[i].label);
    Serial.print("=");
    logDateTime(b);
    Serial.print(", ");
    Serial.print(_candidates[slot].label);
    Serial.print("=");
    logDateTime(a);
    Serial.print(" - applying earlier (");
    Serial.print(earlier_label);
    Serial.println(")");

    for (int j = 0; j < _num_candidates; j++) {
      if (j == slot || j == i || !_candidates[j].have_response) continue;
      Serial.print("[time sync] outlier ignored: ");
      Serial.print(_candidates[j].label);
      Serial.print(" reported ");
      logDateTime(_candidates[j].timestamp);
      Serial.println();
    }

    resetCandidates();
    _state = State::IDLE;
    return;
  }

  // No agreeing pair found - nothing gets discarded (a third source might
  // still agree with one of these), but every candidate that disagreed with
  // this new arrival is marked distrusted for the rest of the session, so
  // the single-source fallback can never blindly trust it alone later.
  for (int i = 0; i < _num_candidates; i++) {
    if (i == slot || !_candidates[i].have_response) continue;

    _candidates[slot].distrusted = true;
    _candidates[i].distrusted = true;

    uint32_t a = _candidates[slot].timestamp, b = _candidates[i].timestamp;
    uint32_t diff = a > b ? a - b : b - a;
    Serial.print("[time sync] sources disagree (delta ");
    Serial.print(diff);
    Serial.print("s > TIME_AGREEMENT_WINDOW_SECS=");
    Serial.print((uint32_t)TIME_AGREEMENT_WINDOW_SECS);
    Serial.print("s): ");
    Serial.print(_candidates[i].label);
    Serial.print("=");
    logDateTime(b);
    Serial.print(", ");
    Serial.print(_candidates[slot].label);
    Serial.print("=");
    logDateTime(a);
    Serial.println(" - both now distrusted for single-source fallback, still watching for a 3rd");
  }
}

void BootTimeSync::retryIfNeeded(SensorMesh& mesh) {
  if (_synced) return;
  if (_state != State::IDLE) return;   // previous attempt still in flight - let it resolve first

  uint8_t data[10];
  data[0] = CTL_TYPE_NODE_DISCOVER_REQ;   // prefix_only=0 (need the full pubkey to compute a shared secret)
  data[1] = (1 << ADV_TYPE_REPEATER);     // filter: repeaters only
  mesh.getRNG()->random(&data[2], 4);     // tag
  memcpy(&_discover_tag, &data[2], 4);
  uint32_t since = 0;
  memcpy(&data[6], &since, 4);

  auto pkt = mesh.createControlData(data, sizeof(data));
  if (pkt) {
    mesh.sendZeroHop(pkt);
    Serial.print("[time sync] still unsynced - discovery sent (attempt ");
    Serial.print(_sync_attempts + 1);
    Serial.println("), listening for repeaters");
    _state = State::WAITING_FOR_DISCOVERY;
    _deadline = millis() + BOOT_SYNC_DISCOVERY_TIMEOUT_MS;
  } else {
    Serial.println("[time sync] ERROR: could not build discovery packet, will retry next cycle");
  }
}

void BootTimeSync::onControlData(SensorMesh& mesh, const mesh::Packet* pkt) {
  if (_state != State::WAITING_FOR_DISCOVERY) return;
  if (pkt->payload_len < 6) return;

  uint8_t type = pkt->payload[0] & 0xF0;
  if (type != CTL_TYPE_NODE_DISCOVER_RESP) return;

  uint8_t node_type = pkt->payload[0] & 0x0F;
  if (node_type != ADV_TYPE_REPEATER) return;

  if (pkt->payload_len < 6 + PUB_KEY_SIZE) return;   // we asked for full pubkeys, not prefix_only

  uint32_t tag;
  memcpy(&tag, &pkt->payload[2], 4);
  if (tag != _discover_tag) return;

  mesh::Identity id(&pkt->payload[6]);
  if (id.matches(mesh.self_id)) return;   // ignore our own reflected packet, if any

#ifdef MESHTEMP_TIMESYNC_DEBUG
  // TEMPORARY DIAGNOSTIC - not committed. Logs EVERY valid discover-resp
  // this attempt, not just the ones we act on, so a cold start shows the
  // true candidate pool size - independent of whether we already asked
  // this one.
  {
    char dbg_hex[9];
    mesh::Utils::toHex(dbg_hex, id.pub_key, 4);
    Serial.print("[ts debug] discover-resp seen: ");
    Serial.print(dbg_hex);
    Serial.print(" our_snr=");
    Serial.println(pkt->getSNR());
  }
#endif

  int slot = findOrAddCandidateSlot(id);
  if (_candidates[slot].requested || _candidates[slot].have_response) return;   // already asked, or already answered (e.g. via a passive advert)

  char hex[9];
  mesh::Utils::toHex(hex, id.pub_key, 4);
  snprintf(_candidates[slot].label, sizeof(_candidates[slot].label), "repeater %s (active)", hex);
  _candidates[slot].requested = true;

  Serial.print("[time sync] repeater found (");
  Serial.print(_candidates[slot].label);
  Serial.println("), requesting its clock");

  sendClockRequestForSlot(mesh, slot);
}

void BootTimeSync::sendClockRequestForSlot(SensorMesh& mesh, int slot) {
  Candidate& c = _candidates[slot];
  ClientInfo* peer = mesh.registerTransientPeer(c.id);
  if (!peer) {
    Serial.print("[time sync] ERROR: could not register ");
    Serial.print(c.label);
    Serial.println(" as peer");
    c.requested = false;
    return;
  }

  uint8_t data[6];
  uint32_t my_now = mesh.getRTCClock()->getCurrentTime();
  memcpy(data, &my_now, 4);          // becomes the reply's tag, per handleAnonClockReq
  data[4] = ANON_REQ_TYPE_BASIC;
  data[5] = 0;                       // reply_path_len=0 -> repeater replies zero-hop, direct back to us
  c.clock_req_tag = my_now;

  auto pkt = mesh.createAnonDatagram(PAYLOAD_TYPE_ANON_REQ, mesh.self_id, c.id, peer->shared_secret, data, sizeof(data));
  if (pkt) {
    mesh.sendZeroHop(pkt);
  } else {
    Serial.print("[time sync] ERROR: could not build clock request for ");
    Serial.println(c.label);
    c.requested = false;
  }
}

void BootTimeSync::onPeerResponse(SensorMesh& mesh, const ClientInfo* from, const uint8_t* data, size_t len) {
#ifdef MESHTEMP_TIMESYNC_DEBUG
  // TEMPORARY DIAGNOSTIC - not committed. Reaching this function at all
  // already proves the reply passed the core dispatch's dest_hash match +
  // searchPeersByHash() + MACThenDecrypt() (src/Mesh.cpp:126-176) - so if
  // this never prints for a given repeater, the reply either never arrived
  // over the air, or failed ACL/decrypt before reaching us at all. Combined
  // with the "[ts debug] raw PAYLOAD_TYPE_RESPONSE seen" line in logRx()
  // below (fires BEFORE ACL/decrypt), the two together localize which side
  // of that boundary a given failure is on.
  {
    char from_hex[65];
    mesh::Utils::toHex(from_hex, from->id.pub_key, PUB_KEY_SIZE);
    Serial.print("[ts debug] onPeerResponse entered: from=");
    Serial.print(from_hex);
    Serial.print(" len=");
    Serial.println((unsigned)len);
  }
#endif
  if (len < 8) {
#ifdef MESHTEMP_TIMESYNC_DEBUG
    Serial.print("[ts debug] onPeerResponse: rejected, len too short: ");
    Serial.println((unsigned)len);
#endif
    return;
  }

  int slot = -1;
  for (int i = 0; i < _num_candidates; i++) {
    if (_candidates[i].have_id && _candidates[i].requested && !_candidates[i].have_response
        && _candidates[i].id.matches(from->id)) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
#ifdef MESHTEMP_TIMESYNC_DEBUG
    Serial.println("[ts debug] onPeerResponse: rejected, no matching requested/unanswered candidate slot for this sender");
#endif
    return;
  }

  uint32_t tag;
  memcpy(&tag, data, 4);
  if (tag != _candidates[slot].clock_req_tag) {
#ifdef MESHTEMP_TIMESYNC_DEBUG
    Serial.print("[ts debug] onPeerResponse: rejected, tag mismatch: got ");
    Serial.print(tag);
    Serial.print(" expected ");
    Serial.println(_candidates[slot].clock_req_tag);
#endif
    return;
  }

  uint32_t repeater_now;
  memcpy(&repeater_now, &data[4], 4);

  // proposeTime() re-finds this same slot (identity already registered
  // above) and does the phase 1/2 evaluation - single place that logic
  // lives, shared with the passive advert path.
  proposeTime(mesh, _candidates[slot].id, repeater_now, _candidates[slot].label);
}

void BootTimeSync::finishAttempt(SensorMesh& mesh) {
  _sync_attempts++;
  int valid = countValidCandidates();

  if (valid == 0) {
    Serial.println("[time sync] no repeater responded this attempt - will retry next cycle");
  } else if (valid == 1) {
    int i = -1;
    for (int k = 0; k < _num_candidates; k++) {
      if (_candidates[k].have_response) { i = k; break; }
    }
    if (_sync_attempts >= 2) {
      if (!_candidates[i].distrusted) {
        mesh.getRTCClock()->setCurrentTime(_candidates[i].timestamp);
        _synced = true;
        Serial.print("[time sync] single-source (no second source after ");
        Serial.print(_sync_attempts);
        Serial.print(" attempts) - accepting ");
        Serial.print(_candidates[i].label);
        Serial.print(": ");
        logDateTime(_candidates[i].timestamp);
        Serial.println();
        resetCandidates();
      } else {
        Serial.print("[time sync] single-source fallback withheld: ");
        Serial.print(_candidates[i].label);
        Serial.println(" already lost a disagreement with another source this session - waiting for corroboration, will retry next cycle");
      }
    } else {
      Serial.print("[time sync] have 1 source so far (");
      Serial.print(_candidates[i].label);
      Serial.println(") - waiting for a second, will retry next cycle");
      // keep this candidate; more slots stay open for a future attempt to
      // find a second, different source.
    }
  } else {
    // valid >= 2 with no sync means no pair has agreed yet - proposeTime()
    // already fires the moment any pair does agree, so reaching here just
    // means: keep the whole pool, keep listening.
    Serial.print("[time sync] no agreeing pair yet among ");
    Serial.print(valid);
    Serial.println(" candidate(s) - still watching");
  }

  _state = State::IDLE;
}

void BootTimeSync::tick(SensorMesh& mesh) {
  if (_synced) return;   // active ladder's job is done; ongoing correction is proposeTime()'s phase 2, driven by the passive path

  if (_state == State::WAITING_FOR_DISCOVERY) {
    if ((long)(millis() - _deadline) >= 0) {
      bool any_outstanding = false;
      for (int i = 0; i < _num_candidates; i++) {
        if (_candidates[i].requested && !_candidates[i].have_response) { any_outstanding = true; break; }
      }
      if (any_outstanding) {
        _state = State::WAITING_FOR_CLOCK;
        _deadline = millis() + BOOT_SYNC_CLOCK_TIMEOUT_MS;
      } else {
        finishAttempt(mesh);
      }
    }
  } else if (_state == State::WAITING_FOR_CLOCK) {
    if ((long)(millis() - _deadline) >= 0) {
      for (int i = 0; i < _num_candidates; i++) {
        if (_candidates[i].have_id && _candidates[i].requested && !_candidates[i].have_response) {
          Serial.print("[time sync] no clock reply from ");
          Serial.println(_candidates[i].label);
          _candidates[i] = Candidate();   // free this slot for a future attempt
        }
      }
      finishAttempt(mesh);
    }
  }
}
