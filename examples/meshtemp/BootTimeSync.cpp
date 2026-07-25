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

bool BootTimeSync::applyIfNewer(SensorMesh& mesh, uint32_t new_time, const char* source_label) {
  uint32_t curr = mesh.getRTCClock()->getCurrentTime();
  if (new_time <= curr) {
    Serial.print("[time sync] ");
    Serial.print(source_label);
    Serial.println(" not ahead of our clock, ignored");
    return false;
  }

  // +1 mirrors CommonCLI's own "clock sync" command convention exactly
  // (src/helpers/CommonCLI.cpp) - same forward-only semantics, same fudge.
  uint32_t applied = new_time + 1;
  mesh.getRTCClock()->setCurrentTime(applied);

  bool was_synced = _synced;
  _synced = true;

  Serial.print("[time sync] ");
  Serial.print(was_synced ? "re-synced from " : "synced from ");
  Serial.print(source_label);
  Serial.print(": ");
  logDateTime(curr);
  Serial.print(" -> ");
  logDateTime(applied);
  Serial.println();
  return true;
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
    Serial.println("[time sync] still unsynced - discovery sent, listening for a repeater");
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

  _repeater_id = id;
  Serial.println("[time sync] repeater found, requesting its clock");
  sendClockRequest(mesh);
}

void BootTimeSync::sendClockRequest(SensorMesh& mesh) {
  ClientInfo* peer = mesh.registerTransientPeer(_repeater_id);
  if (!peer) {
    Serial.println("[time sync] ERROR: could not register repeater as peer, will retry next cycle");
    _state = State::IDLE;
    return;
  }

  uint8_t data[6];
  uint32_t my_now = mesh.getRTCClock()->getCurrentTime();
  memcpy(data, &my_now, 4);          // becomes the reply's tag, per handleAnonClockReq
  data[4] = ANON_REQ_TYPE_BASIC;
  data[5] = 0;                       // reply_path_len=0 -> repeater replies zero-hop, direct back to us
  _clock_req_tag = my_now;

  auto pkt = mesh.createAnonDatagram(PAYLOAD_TYPE_ANON_REQ, mesh.self_id, _repeater_id, peer->shared_secret, data, sizeof(data));
  if (pkt) {
    mesh.sendZeroHop(pkt);
    _state = State::WAITING_FOR_CLOCK;
    _deadline = millis() + BOOT_SYNC_CLOCK_TIMEOUT_MS;
  } else {
    Serial.println("[time sync] ERROR: could not build clock request packet, will retry next cycle");
    _state = State::IDLE;
  }
}

void BootTimeSync::onPeerResponse(SensorMesh& mesh, const ClientInfo* from, const uint8_t* data, size_t len) {
  if (_state != State::WAITING_FOR_CLOCK) return;
  if (!from->id.matches(_repeater_id)) return;
  if (len < 8) return;

  uint32_t tag;
  memcpy(&tag, data, 4);
  if (tag != _clock_req_tag) return;

  uint32_t repeater_now;
  memcpy(&repeater_now, &data[4], 4);

  // The discovery response only carries a pubkey, not a human name - use a
  // short hex prefix to identify which repeater this was, in the log.
  char id_hex[9];
  mesh::Utils::toHex(id_hex, _repeater_id.pub_key, 4);
  char label[48];
  snprintf(label, sizeof(label), "repeater %s (active request)", id_hex);

  applyIfNewer(mesh, repeater_now, label);
  _state = State::IDLE;   // attempt resolved either way; retryIfNeeded() no-ops from here if now synced()
}

void BootTimeSync::tick(SensorMesh& mesh) {
  if (_state == State::WAITING_FOR_DISCOVERY) {
    if ((long)(millis() - _deadline) >= 0) {
      Serial.println("[time sync] no repeater found this attempt - will retry next cycle");
      _state = State::IDLE;
    }
  } else if (_state == State::WAITING_FOR_CLOCK) {
    if ((long)(millis() - _deadline) >= 0) {
      Serial.println("[time sync] repeater found but no clock reply - will retry next cycle");
      _state = State::IDLE;
    }
  }
}
