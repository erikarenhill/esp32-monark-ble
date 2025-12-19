#pragma once
#include "PowerSource.h"
#include "Calibration.h"
#include <Arduino.h>

// ADC sampling: 3Hz, report 1-second average (3 samples)
static const uint8_t ADC_BUF_SIZE = 10;
static const uint32_t ADC_SAMPLE_INTERVAL_MS = 100;  // 10Hz sampling

class PowerReal : public PowerSource {
public:
  PowerReal(float cycleConstant, uint8_t pinCadence, uint8_t pinAdc, ICalibration* calibration);

  void begin() override;
  void update(uint32_t now_ms) override;
  bool hasSample() const override;
  PowerSample getSample() override;

  // Voltage divider conversion utilities (4.7k + 10k divider)
  static float millivoltsToRawAdc(float mv);   // mV at ADC pin -> raw ADC (0-4095)
  static float rawAdcToMillivolts(float raw);  // raw ADC (0-4095) -> mV at ADC pin

private:
  float cycle_constant;
  uint8_t pin_cadence;
  uint8_t pin_adc;
  ICalibration* cal;

  uint32_t last_update_ms = 0;
  uint32_t last_adc_sample_ms = 0;
  bool ready = false;
  PowerSample sample{};

  // ADC ring buffer (3 samples for 1-second average)
  float adcBuffer[ADC_BUF_SIZE] = {0};
  uint8_t adcBufferHead = 0;
  uint8_t adcBufferCount = 0;

  // Cadence smoothing state
  float lastSmoothedRpm = 0.0f;

  // ADC / KP helpers
  void sampleAdc(uint32_t now_ms);
  float readAdcRaw();
  float getSmoothedAdc(uint32_t now_ms);
  float readKp(float& rawAdc, uint32_t now_ms);

  // RPM helpers
  float calculateRpmWindow(uint32_t windowMs);  // Calculate RPM for specific window
  float calculateSmoothedRpm();                  // Smart smoothing combining windows
};
