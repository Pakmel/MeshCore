#pragma once

#include <stdint.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Non-blocking DS18B20 wrapper. Conversion timing is driven by millis(),
// never by loop iteration counts - reading before the conversion window
// has elapsed returns a stale or invalid (85.0C power-on-reset) value.
class WaterTempSensor {
public:
  explicit WaterTempSensor(uint8_t one_wire_pin);

  // Scans the 1-Wire bus and sets 9-bit resolution. Returns true only if
  // a DS18B20 actually responded - never assume presence.
  bool begin();

  bool detected() const { return _detected; }

  // Starts an async temperature conversion. No-op if no sensor was
  // detected at begin().
  void startConversion();

  bool converting() const { return _converting; }

  // True once at least the 9-bit conversion time (per the DallasTemperature
  // library's own millisToWaitForConversion(9), not a guessed constant)
  // has elapsed since startConversion().
  bool conversionDone() const;

  // Only call once conversionDone() is true. Clears converting().
  float readTempC();

private:
  OneWire _wire;
  DallasTemperature _sensors;
  DeviceAddress _address = {0};
  bool _detected = false;
  bool _converting = false;
  unsigned long _conversion_started_at = 0;
};
