#pragma once
#include "PowerSource.h"
#include "Calibration.h"
#include <Arduino.h>

class PowerReal : public PowerSource {
public:
  PowerReal(float cycleConstant, uint8_t pinCadence, uint8_t pinAdc, ICalibration* calibration);

  void begin() override;
  void update(uint32_t now_ms) override;
  bool hasSample() const override;
  PowerSample getSample() override;

private:
  float cycle_constant;
  uint8_t pin_cadence;
  uint8_t pin_adc;
  ICalibration* cal;

  uint32_t last_update_ms = 0;
  bool ready = false;
  PowerSample sample{};

  // ADC / KP helpers
  float readKp(uint16_t& rawAdc);
  
  // RPM helpers
  float calculateRpm();
};
