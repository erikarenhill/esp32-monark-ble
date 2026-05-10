#pragma once
#include "PowerSource.h"
#include "Calibration.h"
#include <Arduino.h>

// ADC sampling: 10Hz raw read, fed into an EMA whose time-constant is
// equivalent to a ~5-sample moving average (≈500 ms window). Replaces the
// old 10-sample (~1 s) ring buffer — drops the ESP-vs-pedal lag from ~2 s
// down to ~1 s while keeping noise rejection adequate.
static const uint32_t ADC_SAMPLE_INTERVAL_MS = 100;
static const float ADC_EMA_ALPHA = 0.333f;  // 2/(N+1) with N=5

class PowerReal : public PowerSource {
public:
  PowerReal(float cycleConstant, uint8_t pinCadence, uint8_t pinAdc, ICalibration* calibration);

  void begin() override;
  void update(uint32_t now_ms) override;
  bool hasSample() const override;
  PowerSample getSample() override;
  void setCycleConstant(float cc) override { cycle_constant = cc; }

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

  // ADC EMA state (replaces ring buffer)
  float adcEma = 0.0f;
  bool adcEmaReady = false;

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
