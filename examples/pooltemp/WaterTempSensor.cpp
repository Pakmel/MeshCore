#include "WaterTempSensor.h"
#include "pooltemp_config.h"

#define DS18B20_RESOLUTION_BITS 9

WaterTempSensor::WaterTempSensor(uint8_t one_wire_pin)
  : _wire(one_wire_pin), _sensors(&_wire) {
}

bool WaterTempSensor::begin() {
  _sensors.begin();
  _sensors.setWaitForConversion(false);  // we drive the wait via millis() ourselves

  _detected = _sensors.getDeviceCount() > 0 && _sensors.getAddress(_address, 0);
  if (_detected) {
    _sensors.setResolution(9);
  }
  return _detected;
}

void WaterTempSensor::startConversion() {
  if (!_detected) return;

  _sensors.requestTemperatures();
  _conversion_started_at = millis();
  _converting = true;
}

bool WaterTempSensor::conversionDone() const {
  if (!_converting) return false;
  // unsigned subtraction: correct across millis() rollover as long as the
  // wait itself is short (it's ~94ms for 9-bit), which it always is here.
  unsigned long elapsed = millis() - _conversion_started_at;
  return elapsed >= DallasTemperature::millisToWaitForConversion(DS18B20_RESOLUTION_BITS);
}

WaterReading WaterTempSensor::readResult() {
  _converting = false;

  // Go via the raw scratchpad value, not getTempC(): this library version
  // already collapses BOTH "disconnected" and "power-on-reset" down to the
  // same DEVICE_DISCONNECTED_C (-127) at the getTempC() level, so the two
  // can only be told apart using the raw sentinels.
  int32_t raw = _sensors.getTemp(_address);

  WaterReading result;
  if (raw == DEVICE_POWER_ON_RESET_RAW) {
    // Read too early / conversion never completed: scratchpad still shows
    // the chip's fixed power-on default (85.0C in raw ticks).
    result.case_type = WaterReadingCase::SENSOR_ERROR;
    result.error_code = 85;
    result.temp_c = 0.0f;
  } else if (raw <= DEVICE_DISCONNECTED_RAW) {
    // No response / bad CRC: wiring or pullup problem.
    result.case_type = WaterReadingCase::SENSOR_ERROR;
    result.error_code = -127;
    result.temp_c = 0.0f;
  } else {
    result.error_code = 0;
    result.temp_c = DallasTemperature::rawToCelsius(raw);
    result.case_type = (result.temp_c < PLAUSIBLE_MIN_C || result.temp_c > PLAUSIBLE_MAX_C)
      ? WaterReadingCase::IMPLAUSIBLE
      : WaterReadingCase::NORMAL;
  }
  return result;
}
