#pragma once

#include <stdint.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// See CLAUDE.md "Message format" for the three cases this maps to.
enum class WaterReadingCase : uint8_t {
  NORMAL,        // case 1: plausible reading
  IMPLAUSIBLE,   // case 2: real reading, outside PLAUSIBLE_MIN_C..PLAUSIBLE_MAX_C
  SENSOR_ERROR   // case 3: no usable reading, error_code is -127 or 85
};

struct WaterReading {
  WaterReadingCase case_type;
  float temp_c;       // meaningful for NORMAL and IMPLAUSIBLE only
  int16_t error_code;  // meaningful for SENSOR_ERROR only: -127 or 85
};

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
  // Classifies the result into one of the three message-contract cases
  // instead of ever handing back a raw library sentinel as if it were a
  // real temperature - a disconnected probe or a power-on-reset scratchpad
  // must never be mistaken for a measurement.
  WaterReading readResult();

private:
  OneWire _wire;
  DallasTemperature _sensors;
  DeviceAddress _address = {0};
  bool _detected = false;
  bool _converting = false;
  unsigned long _conversion_started_at = 0;
};
