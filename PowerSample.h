#pragma once
#include <stdint.h>

struct PowerSample {
  float rpm;          // cadence
  float kp;           // kilopond (virtual)
  float power_w;      // output power (after cycle constant)
  uint16_t crank_revs;
  uint16_t crank_evt_1024;
  uint16_t adc_raw;   // Raw ADC value for calibration
};
