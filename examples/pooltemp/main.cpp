#include "SensorMesh.h"
#include "pooltemp_version.h"
#include "RegionScope.h"

#ifdef POOLTEMP_PIN_DEBUG
  #include "PinScanDebug.h"
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

#ifdef POOLTEMP_DS18B20
  #include "WaterTempSensor.h"
  #include "WaterChannel.h"
  #include "EchoRetry.h"
  #include "BootTimeSync.h"
  #include "pooltemp_config.h"
  #include <helpers/AdvertDataHelpers.h>
  // 1-Wire data pin: the pad silkscreened TX0 on the RAK19003 header, which
  // is nRF52840 P0.20 - PIN_SERIAL2_TX in variants/rak4631/variant.h (the
  // WisBlock connector's TXD0 signal, commented there as "TXD0 RXD0 on Base
  // Board"). NOT PIN_SERIAL1_TX: on this baseplate revision the header pads
  // are silkscreened RX0/TX0 and carry UART0 (TXD0/RXD0 = P0.19/P0.20), which
  // the Arduino core exposes as Serial2, not Serial1. Older RAK19003 Ver.B
  // silkscreened that same header RX1/TX1 and carried UART1 (P0.15/P0.16 =
  // Serial1) - if this node's baseplate reads RX1/TX1, this must be
  // PIN_SERIAL1_TX instead, and then the Serial1 conflict below is real.
  //
  // No UART claims P0.20 in either PoolTemp build, so there is nothing to
  // release. The core does instantiate a global `Uart Serial2` on
  // UARTE1/P0.19/P0.20, but a Uart only routes its pins in Uart::begin(), and
  // nothing in this firmware ever calls Serial2.begin() - only the unrelated
  // RAK_4631_repeater_bridge_rs232_serial2 env does that. Serial1, by
  // contrast, IS begun in this build (P0.15/P0.16, by
  // EnvironmentSensorManager::initBasicGPS() under ENV_INCLUDE_GPS, reached
  // via sensors.begin() in setup()) - which is exactly why the Serial1 pins
  // would have to be freed first and P0.20 does not. The 4.7k pull-up to 3.3V
  // is external, unchanged.
  static WaterTempSensor water_sensor(PIN_SERIAL2_TX);
  static WaterChannel water_channel;
  static WaterReading latest_reading = { WaterReadingCase::SENSOR_ERROR, 0.0f, -127 };  // no reading yet
  static BootTimeSync boot_time_sync;

  // ------------------------------------------------------------------
  // Shared read -> send -> retry cycle. Identical, unconditional code for
  // both RAK_4631_pooltemp (bench test, 2-min interval, no sleep) and
  // RAK_4631_pooltemp_sleep (production, hourly interval, real sleep) -
  // per project decision, the two envs may only differ in the scheduler
  // layer (below, guarded by POOLTEMP_SLEEP_CYCLE) and the interval
  // constant. Nothing in this section is env-conditional.
  // ------------------------------------------------------------------
  enum class CycleState : uint8_t { IDLE, CONVERTING, WAITING_FOR_ECHO };
  static CycleState cycle_state = CycleState::IDLE;
  // millis() timestamp this cycle started at - the fixed interval to the
  // next cycle is always counted from here, not from when this cycle
  // finishes, so a slow cycle (e.g. one that needs a retry) doesn't push
  // out the following cycle's schedule.
  static unsigned long cycle_started_at = 0;
  // Cached exact bytes of the pending transmission, so a retry resends
  // byte-identical content rather than a freshly-read value/timestamp.
  static uint8_t pending_data[5 + POOLTEMP_MSG_MAX_LEN];
  static int pending_data_len = 0;

  // Boot-phase time sync state. After a cold boot the node stays awake and
  // sends nothing until the clock is trusted, so the very first reading to
  // reach the channel carries a real timestamp instead of 0. Deliberately
  // NOT guarded by POOLTEMP_SLEEP_CYCLE: both envs run it, so the bench
  // build remains a faithful test of what production actually does. That
  // matters here specifically - the defect this fixes was invisible on the
  // bench precisely because the bench build never slept.
  static bool boot_sync_phase = true;
  static unsigned long boot_sync_deadline = 0;
  static unsigned long next_sync_retry_due = 0;

  #ifndef POOLTEMP_SLEEP_CYCLE
  // Bench-test-only scheduling state (see the non-sleep branch in loop()).
  static unsigned long next_test_read_due = 0;
  static unsigned long next_test_send_due = 0;
  #define POOLTEMP_TEST_READ_INTERVAL_MS (10UL * 1000UL)
  // Bench-test cadence: every 2 minutes. The real once-per-hour cadence is
  // POOLTEMP_SEND_INTERVAL_SECS, from pooltemp_config.h - only
  // RAK_4631_pooltemp_sleep uses it.
  #define POOLTEMP_TEST_SEND_INTERVAL_MS (2UL * 60UL * 1000UL)
  #endif
#endif

class MyMesh : public SensorMesh {
public:
  MyMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables)
     : SensorMesh(board, radio, ms, rng, rtc, tables),
       battery_data(12*24, 5*60)    // 24 hours worth of battery data, every 5 minutes
  {
  }

#ifdef POOLTEMP_DS18B20
  // Public so the send-cycle state machine in main.cpp loop() can arm/
  // check/disarm it directly.
  EchoRetry echo_retry;
#endif

protected:
#ifdef POOLTEMP_DS18B20
  // Fires for every raw incoming packet BEFORE MeshCore's own flood-dedup
  // (hasSeen()) would otherwise silently swallow our own echoed packet as
  // a "duplicate" - see EchoRetry.h. Dispatcher::logRx() is a no-op by
  // default, so no base call is needed.
  void logRx(mesh::Packet* pkt, int len, float score) override {
    if (echo_retry.armed()) {
      uint8_t hash[MAX_HASH_SIZE];
      pkt->calculatePacketHash(hash);
      echo_retry.onPacketSeen(hash);
    }
#ifdef POOLTEMP_TIMESYNC_DEBUG
    // TEMPORARY DIAGNOSTIC - not committed. Fires for every raw incoming
    // packet, before dedup/ACL/decrypt - independent of echo_retry above.
    // Answers: does a PAYLOAD_TYPE_RESPONSE-shaped packet arrive at all,
    // regardless of whether our ACL/decrypt matching later accepts it -
    // compare against "[ts debug] onPeerResponse entered" in
    // BootTimeSync.cpp to localize a failure to before/after that boundary.
    if (pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE && pkt->payload_len >= 2) {
      Serial.print("[ts debug] raw PAYLOAD_TYPE_RESPONSE seen: dest_hash=");
      Serial.print(pkt->payload[0], HEX);
      Serial.print(" src_hash=");
      Serial.print(pkt->payload[1], HEX);
      Serial.print(" payload_len=");
      Serial.print(pkt->payload_len);
      Serial.print(" route=");
      Serial.println(pkt->isRouteDirect() ? "direct" : (pkt->isRouteFlood() ? "flood" : "?"));
    }
#endif
  }

  // Passive time sync (bonus layer, independent of BootTimeSync's active
  // discovery+request flow): any heard repeater advert also carries a
  // usable timestamp. Both paths funnel through
  // boot_time_sync.proposeTime(), so a lone advert can't seed the clock
  // alone before the first sync any more than a lone active reply can - see
  // BootTimeSync.h for the full two-source-then-sanity-window design.
  void onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp,
                     const uint8_t* app_data, size_t app_data_len) override {
    AdvertDataParser parser(app_data, app_data_len);
    if (parser.isValid() && parser.getType() == ADV_TYPE_REPEATER) {
      boot_time_sync.proposeTime(*this, id, timestamp, parser.hasName() ? parser.getName() : "repeater advert");
    }
  }

  // Routes CTL_TYPE_NODE_DISCOVER_RESP packets to boot_time_sync; chains to
  // SensorMesh's own override (answers "who's a sensor" requests about us -
  // unrelated, left untouched) either way.
  void onControlDataRecv(mesh::Packet* packet) override {
    boot_time_sync.onControlData(*this, packet);
    SensorMesh::onControlDataRecv(packet);
  }

  // Routes our own clock-request's PAYLOAD_TYPE_RESPONSE reply to
  // boot_time_sync; chains to SensorMesh's own override (handles
  // PAYLOAD_TYPE_REQ / admin TXT_MSG commands from real contacts -
  // unrelated, left untouched) either way.
  void onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx, const uint8_t* secret, uint8_t* data, size_t len) override {
    if (type == PAYLOAD_TYPE_RESPONSE) {
      ClientInfo* from = resolvePeer(sender_idx);
      if (from) boot_time_sync.onPeerResponse(*this, from, data, len);
    }
    SensorMesh::onPeerDataRecv(packet, type, sender_idx, secret, data, len);
  }
#endif

  /* ========================== custom logic here ========================== */
  Trigger low_batt, critical_batt;
  TimeSeriesData  battery_data;

  void onSensorDataRead() override {
    float batt_voltage = getVoltage(TELEM_CHANNEL_SELF);

    battery_data.recordData(getRTCClock(), batt_voltage);   // record battery
    alertIf(batt_voltage < 3.4f, critical_batt, HIGH_PRI_ALERT, "Battery is critical!");
    alertIf(batt_voltage < 3.6f, low_batt, LOW_PRI_ALERT, "Battery is low");
  }

  int querySeriesData(uint32_t start_secs_ago, uint32_t end_secs_ago, MinMaxAvg dest[], int max_num) override {
    battery_data.calcMinMaxAvg(getRTCClock(), start_secs_ago, end_secs_ago, &dest[0], TELEM_CHANNEL_SELF, LPP_VOLTAGE);
    return 1;
  }

  bool handleCustomCommand(uint32_t sender_timestamp, char* command, char* reply) override {
    if (strcmp(command, "magic") == 0) {    // example 'custom' command handling
      strcpy(reply, "**Magic now done**");
      return true;   // handled
    }
    return false;  // not handled
  }
  /* ======================================================================= */
};

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

#ifdef POOLTEMP_DS18B20
// Builds and transmits a GRP_TXT packet on the water channel. Returns the
// packet's own hash via `out_hash` (MAX_HASH_SIZE bytes) so the caller can
// arm the echo watch - computed before sendFlood() hands the packet off,
// since its lifecycle belongs to the dispatcher/packet manager afterwards.
static bool pooltempSendChannelData(const uint8_t* data, int data_len, uint8_t* out_hash) {
  auto pkt = the_mesh.createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, water_channel.channel, data, data_len);
  if (!pkt) return false;

  pkt->calculatePacketHash(out_hash);
  if (RegionScope::active()) {
    uint16_t codes[2];
    RegionScope::codesFor(pkt, codes);
    the_mesh.sendFlood(pkt, codes);
  } else {
    the_mesh.sendFlood(pkt);
  }
  return true;
}

// Formats latest_reading + current battery voltage per the message
// contract, sends it, and arms the echo watch on success. Returns false
// if the packet couldn't even be built (nothing to wait for then).
static bool pooltempSendCurrentReadingAndArm() {
  float batt_v = board.getBattMilliVolts() / 1000.0f;
  char msg[POOLTEMP_MSG_MAX_LEN];
  WaterChannel::formatMessage(msg, sizeof(msg), latest_reading, batt_v);
  int msg_len = strlen(msg);

  // If the clock has never been synced since cold boot, its value is just
  // VolatileRTCClock's hardcoded startup default (a fixed, wrong date - see
  // src/helpers/ArduinoHelpers.h), not real time. Sending it would look like
  // a plausible-but-wrong reading date to anyone downstream. Send 0 instead
  // - an obviously-unset sentinel - never withhold the reading itself over
  // this; a wrong timestamp is data, a withheld reading is data loss.
  uint32_t timestamp = boot_time_sync.synced() ? the_mesh.getRTCClock()->getCurrentTime() : 0;
  if (timestamp == 0) {
    Serial.println("[time sync] still unsynced - sending reading with timestamp=0");
  }
  memcpy(pending_data, &timestamp, 4);
  pending_data[4] = 0;  // flags/attempt byte, unused here
  memcpy(&pending_data[5], msg, msg_len);
  pending_data_len = 5 + msg_len;

  uint8_t sent_hash[MAX_HASH_SIZE];
  if (pooltempSendChannelData(pending_data, pending_data_len, sent_hash)) {
    the_mesh.echo_retry.arm(sent_hash, (unsigned long)RETRY_WINDOW_S * 1000UL);
    Serial.print("[channel send] ");
    Serial.println(msg);
    return true;
  }

  Serial.println("[channel send] ERROR: unable to build packet (message too long?)");
  return false;
}

// Starts a new cycle: DS18B20 conversion if a probe was detected at boot,
// otherwise straight to a case-3 sensor-error send (a probe that's been
// missing since boot is exactly the "not responding" fault case 3 exists
// for - it must not go silent forever). No-op if a cycle is already
// running (never stacks cycles).
static void pooltempStartCycle() {
  if (cycle_state != CycleState::IDLE) return;

  cycle_started_at = millis();

  // Time sync runs first, but is never a gate: this only fires a discovery
  // broadcast and returns immediately (no-op once already synced()) - it
  // never delays or blocks the read/send below, on this cycle or any other.
  boot_time_sync.retryIfNeeded(the_mesh);

  if (water_sensor.detected()) {
    water_sensor.startConversion();
    cycle_state = CycleState::CONVERTING;
  } else {
    latest_reading = { WaterReadingCase::SENSOR_ERROR, 0.0f, -127 };
    cycle_state = pooltempSendCurrentReadingAndArm() ? CycleState::WAITING_FOR_ECHO : CycleState::IDLE;
  }
}

// Call every loop() iteration. Advances CONVERTING -> WAITING_FOR_ECHO ->
// IDLE. No-op when already IDLE - callers use pooltempCycleIdle() to know
// when it's safe to schedule the next cycle.
static void pooltempCycleTick() {
  switch (cycle_state) {
    case CycleState::IDLE:
      break;

    case CycleState::CONVERTING:
      if (water_sensor.conversionDone()) {
        latest_reading = water_sensor.readResult();
        cycle_state = pooltempSendCurrentReadingAndArm() ? CycleState::WAITING_FOR_ECHO : CycleState::IDLE;
      }
      break;

    case CycleState::WAITING_FOR_ECHO:
      if (the_mesh.echo_retry.echoHeard()) {
        Serial.println("[retry] repeat heard");
        the_mesh.echo_retry.disarm();
        cycle_state = CycleState::IDLE;
      } else if (the_mesh.echo_retry.windowExpired()) {
        // Never more than one retry: disarm before resending so the
        // retry's own transmission can't re-trigger this branch.
        the_mesh.echo_retry.disarm();

        uint8_t retry_hash[MAX_HASH_SIZE];
        if (pooltempSendChannelData(pending_data, pending_data_len, retry_hash)) {
          Serial.println("[retry] retry sent");
        } else {
          Serial.println("[retry] gave up");
        }
        // Done regardless of the retry's own outcome - no second echo wait.
        cycle_state = CycleState::IDLE;
      }
      break;
  }
}

static bool pooltempCycleIdle() {
  return cycle_state == CycleState::IDLE;
}

#ifdef POOLTEMP_SLEEP_CYCLE
// ------------------------------------------------------------------
// Scheduler layer, production build only. Everything above this point
// is shared with RAK_4631_pooltemp unchanged.
// ------------------------------------------------------------------

// Re-establishes the radio after CustomSX1262Wrapper::powerOff()'s cold
// sleep(false), which drops the chip's RF config entirely. radio_init()
// alone is NOT enough: RadioLibWrapper tracks its own RX/TX state in a
// file-static variable that powerOff() does not touch, so without calling
// radio_driver.begin() again (which resets that tracking to STATE_IDLE),
// the dispatcher's checkRecv() would wrongly believe the radio is still
// in RX and never call startReceive() again after wake. Verified by
// reading src/helpers/radiolib/RadioLibWrappers.cpp - not assumed.
static void pooltempReinitRadioAfterSleep() {
  radio_init();
  radio_driver.begin();
  NodePrefs* prefs = the_mesh.getNodePrefs();
  radio_driver.setParams(prefs->freq, prefs->bw, prefs->sf, prefs->cr);
  radio_driver.setTxPower(prefs->tx_power_dbm);
}

// Powers the radio down, then naps in short untimed WFE bursts
// (board.sleep(0) - nRF52 ignores the argument and wakes on any
// interrupt) until cycle_started_at + POOLTEMP_SEND_INTERVAL_SECS. Fixed
// interval counted from this cycle's start (not from now), per project
// decision - VolatileRTCClock has no wall clock to align to anyway.
static void pooltempSleepUntilNextCycle() {
  unsigned long awake_ms = millis() - cycle_started_at;
  Serial.print("[sleep] awake for ");
  Serial.print(awake_ms);
  Serial.println(" ms, powering off radio");

  radio_driver.powerOff();

  unsigned long next_wake_at = cycle_started_at + (POOLTEMP_SEND_INTERVAL_SECS * 1000UL);
  unsigned long sleep_started_at = millis();
  while ((long)(millis() - next_wake_at) < 0) {
    board.sleep(0);
    rtc_clock.tick();
  }

  Serial.print("[sleep] slept ");
  Serial.print(millis() - sleep_started_at);
  Serial.println(" ms, reinitializing radio");
  pooltempReinitRadioAfterSleep();
}
#endif
#endif

static char command[160];

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.print("PoolTemp version: ");
  Serial.println(POOLTEMP_VERSION);

  board.begin();

#ifdef POOLTEMP_PIN_DEBUG
  // TEMPORARY - runs right after board.begin() (matches where the real
  // water_sensor.begin() runs later in setup()), before radio/mesh init so
  // it still logs even if a later step hangs. See PinScanDebug.h.
  //
  // A reset also resets the nRF52's USB peripheral, so the host's serial
  // connection drops and has to re-enumerate - a one-shot boot print race
  // the host easily loses. Wait here for the host to actually reconnect
  // (tud_cdc_n_connected() via Serial's operator bool(), the documented
  // `while (!Serial) {}` idiom from Adafruit_USBD_CDC.cpp) before scanning,
  // instead of hoping the timing works out. Capped so a debug flash left
  // running with no monitor attached doesn't hang forever.
  {
    unsigned long wait_start = millis();
    while (!Serial && (millis() - wait_start) < 30000UL) {
      delay(50);
    }
    delay(300);  // let the host's monitor actually start reading, not just enumerate
  }
  pinScanDebug();
#endif

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_driver.getRngSeed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Sensor ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;

  sensors.begin();

  the_mesh.begin(fs);

  // Derive the POOLTEMP_REGION transport key before anything gets sent -
  // both the boot advert below and the channel-message send cycle need it.
  RegionScope::begin();

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif

#ifdef POOLTEMP_DS18B20
  WaterChannel::selfCheckKeyDerivation();
  water_channel.begin();

  if (water_sensor.begin()) {
    Serial.println("DS18B20 detected on TX0 (P0.20)");
  } else {
    Serial.println("DS18B20 NOT detected on TX0 (P0.20) - check wiring");
  }

  // Printed here, not right after board.begin(), because the earlier spot
  // is lost: a reset drops the nRF52's USB CDC connection, the host has to
  // re-enumerate the COM port, and a flat delay(1000) after Serial.begin()
  // is not a wait-for-reconnect - "PoolTemp version:"/"Sensor ID:" (both
  // printed earlier, unconditionally) are lost to the same race. Placed
  // after BOTH the self-check and DS18B20 lines above (not right before
  // just one of them) - a single reconnect-boundary print can still land in
  // a partially-filled USB CDC ring buffer and get dropped even when a
  // neighboring line survives, so this needs real margin past the
  // boundary, not just proximity to one confirmed-reliable line. getResetReason()
  // itself was captured by NRF52Board::initPowerMgr() (via checkBootVoltage()
  // in board.begin()) and is retained in RAM for this whole boot even though
  // the hardware RESETREAS register itself is cleared right after that capture.
  Serial.print("[boot] reset reason: ");
  Serial.println(board.getResetReasonString(board.getResetReason()));

#ifndef POOLTEMP_SLEEP_CYCLE
  next_test_read_due = millis();
  next_test_send_due = millis() + POOLTEMP_TEST_SEND_INTERVAL_MS;
#endif

  // Enter the boot-phase time sync instead of sending straight away. No
  // reading goes out until the clock is trusted (or the ceiling expires), so
  // the first message on the channel carries a real timestamp. The first
  // retry fires immediately; loop() drives the rest.
  boot_sync_deadline = millis() + (POOLTEMP_SYNC_AWAKE_MAX_SECS * 1000UL);
  next_sync_retry_due = millis();
  Serial.print("[time sync] boot phase: staying awake until synced, ceiling ");
  Serial.print(POOLTEMP_SYNC_AWAKE_MAX_SECS);
  Serial.println("s - no reading is sent until the clock is set");
#endif
}

void loop() {
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
    }
    Serial.print(c);
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif

#ifdef POOLTEMP_DS18B20
  boot_time_sync.tick(the_mesh);
  pooltempCycleTick();

  // Boot phase: stay awake, send nothing, drive the sync ladder on its own
  // cadence. Runs before either scheduler below and returns early, so no
  // cycle starts and (on the production build) no sleep happens until the
  // phase ends. It ends exactly once per cold boot, in one of two ways.
  if (boot_sync_phase) {
    if (boot_time_sync.synced()) {
      // Success: fire the first reading right now, with a real timestamp.
      Serial.println("[time sync] boot phase complete - clock set, sending first reading now");
      boot_sync_phase = false;
      pooltempStartCycle();
    } else if ((long)(millis() - boot_sync_deadline) >= 0) {
      // Ceiling hit: no time source reachable. Say so plainly, then behave
      // exactly as before - the reading still goes out (with timestamp 0),
      // and normal cycling keeps retrying via the per-cycle ladder and the
      // passive advert path. Never permanently silent.
      Serial.print("[time sync] boot phase gave up after ");
      Serial.print(POOLTEMP_SYNC_AWAKE_MAX_SECS);
      Serial.println("s with no usable time source - sending with timestamp=0 and entering normal cycling");
      boot_sync_phase = false;
      pooltempStartCycle();
    } else if ((long)(millis() - next_sync_retry_due) >= 0) {
      next_sync_retry_due = millis() + (POOLTEMP_SYNC_RETRY_SECS * 1000UL);
      boot_time_sync.bootPhaseRetry(the_mesh);
    }
    rtc_clock.tick();
    return;   // no cycling, no sleeping, while the boot phase runs
  }

#ifdef POOLTEMP_SLEEP_CYCLE
  // Production scheduler: once the cycle (including any retry) has fully
  // resolved, power off and sleep until the next scheduled wake, then
  // start the next cycle. No timer needed to decide "when" - waking up
  // from pooltempSleepUntilNextCycle() IS the next scheduled cycle time.
  if (pooltempCycleIdle()) {
    pooltempSleepUntilNextCycle();
    pooltempStartCycle();
  }
#else
  // Bench-test scheduler: standalone 10s debug read+print (no send - just
  // a convenience for watching the temperature on serial without waiting
  // a full 2 minutes), plus the 2-minute send-cycle trigger.
  if (water_sensor.detected()) {
    unsigned long now = millis();
    if (!water_sensor.converting() && (long)(now - next_test_read_due) >= 0
        && cycle_state == CycleState::IDLE) {
      water_sensor.startConversion();
    } else if (water_sensor.converting() && water_sensor.conversionDone()
               && cycle_state == CycleState::IDLE) {
      WaterReading debug_reading = water_sensor.readResult();
      float batt_v = board.getBattMilliVolts() / 1000.0f;
      char msg[POOLTEMP_MSG_MAX_LEN];
      WaterChannel::formatMessage(msg, sizeof(msg), debug_reading, batt_v);

      const char* case_label =
        debug_reading.case_type == WaterReadingCase::NORMAL ? "case 1 (normal)" :
        debug_reading.case_type == WaterReadingCase::IMPLAUSIBLE ? "case 2 (implausible)" :
        "case 3 (sensor error)";
      Serial.print("[DS18B20 test] ");
      Serial.print(case_label);
      Serial.print(": ");
      Serial.println(msg);

      next_test_read_due = millis() + POOLTEMP_TEST_READ_INTERVAL_MS;
    }
  }

  if (pooltempCycleIdle() && (long)(millis() - next_test_send_due) >= 0) {
    next_test_send_due = millis() + POOLTEMP_TEST_SEND_INTERVAL_MS;
    pooltempStartCycle();
  }
#endif
#endif

  rtc_clock.tick();
}
