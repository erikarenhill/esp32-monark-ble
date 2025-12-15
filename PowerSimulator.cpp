#include "PowerSimulator.h"
#include <Arduino.h>

PowerSimulator::PowerSimulator(float cc)
: cycle_constant(cc) {}

void PowerSimulator::begin() {
  randomSeed((uint32_t)esp_random());
}

void PowerSimulator::update(uint32_t now_ms) {
  if (now_ms - last_ms < 1000) return;
  last_ms = now_ms;

  int rpm = random(85, 96);
  int pwr = random(150, 201);

  float kp = (float)pwr / ((float)rpm * cycle_constant);

  // crank revolutions
  float revs = rpm / 60.0f;
  rev_accum += revs;
  uint16_t add = (uint16_t)rev_accum;
  if (add > 0) {
    rev_accum -= add;
    sample.crank_revs += add;
    
    // Calculate ticks for these revolutions (1/1024 sec resolution)
    // Time for 'add' revolutions = (add * 60 / rpm) seconds
    // Ticks = Time * 1024
    float ticks = (add * 60.0f / rpm) * 1024.0f;
    sample.crank_evt_1024 += (uint16_t)ticks;
  }

  sample.rpm = rpm;
  sample.kp = kp;
  sample.power_w = pwr;
  sample.adc_raw = 0; // No ADC in simulation

  ready = true;
}

bool PowerSimulator::hasSample() const {
  return ready;
}

PowerSample PowerSimulator::getSample() {
  ready = false;
  return sample;
}
