#include "WaterTempSensor.h"

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

float WaterTempSensor::readTempC() {
  _converting = false;
  return _sensors.getTempC(_address);
}
