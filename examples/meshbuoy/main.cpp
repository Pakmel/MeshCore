#include "SensorMesh.h"
#include "meshbuoy_version.h"

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

#ifdef MESHBUOY_DS18B20
  #include "WaterTempSensor.h"
  #include "WaterChannel.h"
  #include "meshbuoy_config.h"
  static WaterTempSensor water_sensor(WB_IO1);
  static WaterChannel water_channel;
  static WaterReading latest_reading = { WaterReadingCase::SENSOR_ERROR, 0.0f, -127 };  // no reading yet
  static unsigned long next_test_read_due = 0;
  static unsigned long next_test_send_due = 0;
  #define MESHBUOY_TEST_READ_INTERVAL_MS (10UL * 1000UL)
  // Round 3 test cadence only (TASKS.md: "test interval 2 minutes") - the
  // real once-per-hour cadence (MESHBUOY_SEND_INTERVAL_SECS) lands in
  // Round 5's sleep cycle, not here.
  #define MESHBUOY_TEST_SEND_INTERVAL_MS (2UL * 60UL * 1000UL)
#endif

class MyMesh : public SensorMesh {
public:
  MyMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables)
     : SensorMesh(board, radio, ms, rng, rtc, tables), 
       battery_data(12*24, 5*60)    // 24 hours worth of battery data, every 5 minutes
  {
  }

protected:
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

static char command[160];

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.print("MeshBuoy version: ");
  Serial.println(MESHBUOY_VERSION);

  board.begin();

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

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif

#ifdef MESHBUOY_DS18B20
  WaterChannel::selfCheckKeyDerivation();
  water_channel.begin();

  if (water_sensor.begin()) {
    Serial.println("DS18B20 detected on WB_IO1");
  } else {
    Serial.println("DS18B20 NOT detected on WB_IO1 - check wiring");
  }
  next_test_read_due = millis();
  next_test_send_due = millis();
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

#ifdef MESHBUOY_DS18B20
  if (water_sensor.detected()) {
    unsigned long now = millis();
    if (!water_sensor.converting() && (long)(now - next_test_read_due) >= 0) {
      water_sensor.startConversion();
    } else if (water_sensor.converting() && water_sensor.conversionDone()) {
      latest_reading = water_sensor.readResult();
      float batt_v = board.getBattMilliVolts() / 1000.0f;

      char msg[MESHBUOY_MSG_MAX_LEN];
      WaterChannel::formatMessage(msg, sizeof(msg), latest_reading, batt_v);

      const char* case_label =
        latest_reading.case_type == WaterReadingCase::NORMAL ? "case 1 (normal)" :
        latest_reading.case_type == WaterReadingCase::IMPLAUSIBLE ? "case 2 (implausible)" :
        "case 3 (sensor error)";
      Serial.print("[DS18B20 test] ");
      Serial.print(case_label);
      Serial.print(": ");
      Serial.println(msg);

      next_test_read_due = millis() + MESHBUOY_TEST_READ_INTERVAL_MS;
    }
  }

  // Round 3 test-mode channel push: send the latest cached reading every
  // MESHBUOY_TEST_SEND_INTERVAL_MS on the #watertemp hashtag channel.
  if ((long)(millis() - next_test_send_due) >= 0) {
    float batt_v = board.getBattMilliVolts() / 1000.0f;
    char msg[MESHBUOY_MSG_MAX_LEN];
    WaterChannel::formatMessage(msg, sizeof(msg), latest_reading, batt_v);
    int msg_len = strlen(msg);

    uint8_t data[5 + MESHBUOY_MSG_MAX_LEN];
    uint32_t timestamp = the_mesh.getRTCClock()->getCurrentTime();
    memcpy(data, &timestamp, 4);
    data[4] = 0;  // flags/attempt byte, unused here
    memcpy(&data[5], msg, msg_len);

    auto pkt = the_mesh.createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, water_channel.channel, data, 5 + msg_len);
    if (pkt) {
      the_mesh.sendFlood(pkt);
      Serial.print("[channel send] ");
      Serial.println(msg);
    } else {
      Serial.println("[channel send] ERROR: unable to build packet (message too long?)");
    }

    next_test_send_due = millis() + MESHBUOY_TEST_SEND_INTERVAL_MS;
  }
#endif

  rtc_clock.tick();
}
