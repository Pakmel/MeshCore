#include "PinScanDebug.h"

#ifdef MESHTEMP_PIN_DEBUG
#include <Arduino.h>
#include <OneWire.h>

// Raw 1-Wire ROM search, independent of DallasTemperature's own device-count
// scan (WaterTempSensor::begin()) - this logs every ROM found, not just
// whether a DS18B20 responds, and includes a CRC check so a floating/noisy
// pin producing garbage bits is distinguishable from a real device.
static void scanPin(uint8_t pin, const char* label) {
  OneWire wire(pin);

  Serial.print("[pin scan] ");
  Serial.print(label);
  Serial.print(" (pin ");
  Serial.print(pin);
  Serial.println("):");

  wire.reset_search();
  uint8_t addr[8];
  int found = 0;
  while (wire.search(addr)) {
    found++;
    Serial.print("  ROM: ");
    for (int i = 0; i < 8; i++) {
      if (addr[i] < 0x10) Serial.print('0');
      Serial.print(addr[i], HEX);
      if (i < 7) Serial.print(':');
    }
    bool crc_ok = OneWire::crc8(addr, 7) == addr[7];
    Serial.println(crc_ok ? "  (CRC OK)" : "  (CRC MISMATCH - noise, not a real device)");
  }
  if (found == 0) {
    Serial.println("  (no devices found)");
  }
}

void pinScanDebug() {
  Serial.println("[pin scan] starting WB_IO1 / WB_IO2 comparison scan...");
  scanPin(WB_IO1, "WB_IO1");
  // WB_IO2 controls 3.3V power to certain WisBlock modules on some slots -
  // it isn't necessarily a general-purpose 1-Wire-capable data pin - a
  // negative result here doesn't by itself rule out the probe being wired
  // to whatever pad is physically silkscreened IO2, only that it doesn't
  // answer on the WB_IO2 GPIO number.
  Serial.println("[pin scan] NOTE: WB_IO2 is documented as a 3.3V power-switch pin on some");
  Serial.println("[pin scan]       WisBlock modules, not guaranteed general-purpose 1-Wire data.");
  scanPin(WB_IO2, "WB_IO2");
  Serial.println("[pin scan] done");
}
#endif
