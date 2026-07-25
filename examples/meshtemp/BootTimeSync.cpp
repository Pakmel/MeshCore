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
  for (int i = 0; i < 2; i++) {
    if (_candidates[i].have_id && _candidates[i].id.matches(id)) return i;
  }
  for (int i = 0; i < 2; i++) {
    if (!_candidates[i].have_id) {
      _candidates[i].have_id = true;
      _candidates[i].id = id;
      return i;
    }
  }
  return -1;   // both slots already claimed by other, distinct sources
}

int BootTimeSync::countValidCandidates() const {
  int n = 0;
  if (_candidates[0].have_response) n++;
  if (_candidates[1].have_response) n++;
  return n;
}

void BootTimeSync::resetCandidates() {
  _candidates[0] = Candidate();
  _candidates[1] = Candidate();
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
    }
    return;
  }

  // PHASE 1: gather up to 2 independent candidates before trusting any one.
  int slot = findOrAddCandidateSlot(source_id);
  if (slot < 0) return;   // already have 2 distinct sources this round, ignore further ones

  strncpy(_candidates[slot].label, source_label, sizeof(_candidates[slot].label) - 1);
  _candidates[slot].have_response = true;
  _candidates[slot].timestamp = new_time;

  Serial.print("[time sync] candidate: ");
  Serial.print(source_label);
  Serial.print(" reports ");
  logDateTime(new_time);
  Serial.println();

  evaluateTwoIfReady(mesh);
}

void BootTimeSync::evaluateTwoIfReady(SensorMesh& mesh) {
  if (countValidCandidates() < 2) return;

  uint32_t t0 = _candidates[0].timestamp;
  uint32_t t1 = _candidates[1].timestamp;
  uint32_t diff = t0 > t1 ? t0 - t1 : t1 - t0;

  if (diff <= TIME_AGREEMENT_WINDOW_SECS) {
    bool zero_earlier = t0 <= t1;
    uint32_t earlier = zero_earlier ? t0 : t1;
    const char* earlier_label = zero_earlier ? _candidates[0].label : _candidates[1].label;

    mesh.getRTCClock()->setCurrentTime(earlier);
    _synced = true;

    Serial.print("[time sync] synced (2-source agreement, delta ");
    Serial.print(diff);
    Serial.print("s): ");
    Serial.print(_candidates[0].label);
    Serial.print("=");
    logDateTime(t0);
    Serial.print(", ");
    Serial.print(_candidates[1].label);
    Serial.print("=");
    logDateTime(t1);
    Serial.print(" - applying earlier (");
    Serial.print(earlier_label);
    Serial.println(")");
  } else {
    Serial.print("[time sync] sources disagree (delta ");
    Serial.print(diff);
    Serial.print("s > TIME_AGREEMENT_WINDOW_SECS=");
    Serial.print((uint32_t)TIME_AGREEMENT_WINDOW_SECS);
    Serial.print("s): ");
    Serial.print(_candidates[0].label);
    Serial.print("=");
    logDateTime(t0);
    Serial.print(", ");
    Serial.print(_candidates[1].label);
    Serial.print("=");
    logDateTime(t1);
    Serial.println(" - trusting neither, retrying");
  }

  resetCandidates();
  _state = State::IDLE;
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

  int slot = findOrAddCandidateSlot(id);
  if (slot < 0) return;   // already have 2 distinct candidates this attempt

  if (_candidates[slot].requested || _candidates[slot].have_response) return;   // already asked, or already answered (e.g. via a passive advert)

  char hex[9];
  mesh::Utils::toHex(hex, id.pub_key, 4);
  snprintf(_candidates[slot].label, sizeof(_candidates[slot].label), "repeater %s (active)", hex);
  _candidates[slot].requested = true;

  Serial.print("[time sync] repeater found (");
  Serial.print(_candidates[slot].label);
  Serial.println("), requesting its clock");

  sendClockRequestForSlot(mesh, slot);

  if (_candidates[0].have_id && _candidates[1].have_id) {
    // Both slots claimed (whether via a request just sent, or a passive
    // advert that already answered one of them) - stop listening for more
    // discovery responses this attempt and give outstanding requests their
    // own window.
    _state = State::WAITING_FOR_CLOCK;
    _deadline = millis() + BOOT_SYNC_CLOCK_TIMEOUT_MS;
  }
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
  if (len < 8) return;

  int slot = -1;
  for (int i = 0; i < 2; i++) {
    if (_candidates[i].have_id && _candidates[i].requested && !_candidates[i].have_response
        && _candidates[i].id.matches(from->id)) {
      slot = i;
      break;
    }
  }
  if (slot < 0) return;

  uint32_t tag;
  memcpy(&tag, data, 4);
  if (tag != _candidates[slot].clock_req_tag) return;

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

  if (valid >= 2) {
    // Shouldn't normally reach here - evaluateTwoIfReady() already fires as
    // soon as the 2nd response lands - but cover it defensively.
    evaluateTwoIfReady(mesh);
    _state = State::IDLE;
    return;
  }

  if (valid == 1) {
    int i = _candidates[0].have_response ? 0 : 1;
    if (_sync_attempts >= 2) {
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
      Serial.print("[time sync] have 1 source so far (");
      Serial.print(_candidates[i].label);
      Serial.println(") - waiting for a second, will retry next cycle");
      // keep this candidate; the other (empty) slot stays open for a
      // future attempt to find a second, different source.
    }
  } else {
    Serial.println("[time sync] no repeater responded this attempt - will retry next cycle");
    resetCandidates();
  }

  _state = State::IDLE;
}

void BootTimeSync::tick(SensorMesh& mesh) {
  if (_synced) return;   // active ladder's job is done; ongoing correction is proposeTime()'s phase 2, driven by the passive path

  if (_state == State::WAITING_FOR_DISCOVERY) {
    if ((long)(millis() - _deadline) >= 0) {
      bool any_requested = _candidates[0].requested || _candidates[1].requested;
      if (any_requested) {
        _state = State::WAITING_FOR_CLOCK;
        _deadline = millis() + BOOT_SYNC_CLOCK_TIMEOUT_MS;
      } else {
        finishAttempt(mesh);
      }
    }
  } else if (_state == State::WAITING_FOR_CLOCK) {
    if ((long)(millis() - _deadline) >= 0) {
      for (int i = 0; i < 2; i++) {
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
