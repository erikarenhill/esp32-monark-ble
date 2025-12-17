#include "PowerReal.h"

// ------------------ Configuration ------------------
// Number of samples for single ADC read (noise reduction)
static const uint8_t ADC_SAMPLES = 8;

// Cadence tuning
static const uint32_t EDGE_DEBOUNCE_MS   = 12;
static const uint32_t MIN_REV_PERIOD_MS  = 200;
static const uint32_t WINDOW_SHORT_MS    = 3000;   // 3s window (responsive)
static const uint32_t WINDOW_LONG_MS     = 10000;  // 10s window (smooth)
static const uint32_t TIMEOUT_MS         = 5000;

// ------------------ ISR & Globals ------------------
// We use static/global variables for the ISR because attaching a class member is complex
static volatile uint8_t  g_pin_cadence = 0;

static const uint8_t TS_BUF_SIZE = 64;  // Enough for 10s at high cadence
static volatile uint32_t ts_buf[TS_BUF_SIZE];
static volatile uint8_t  ts_head = 0;
static volatile uint8_t  ts_count = 0;

static volatile uint32_t last_edge_ms = 0;
static volatile uint32_t last_rev_ms = 0;
static volatile bool armed_for_count = true;
static volatile uint32_t total_revs = 0; // Cumulative revolutions
static volatile uint32_t isr_calls = 0;  // Debug: count ISR calls

static inline void push_timestamp(uint32_t t_ms) {
  ts_buf[ts_head] = t_ms;
  ts_head = (ts_head + 1) % TS_BUF_SIZE;
  if (ts_count < TS_BUF_SIZE) ts_count++;
}

// ISR
void IRAM_ATTR cadenceISR() {
  isr_calls++;  // Debug: count every ISR call
  uint32_t now = millis();

  if ((now - last_edge_ms) < EDGE_DEBOUNCE_MS) return;
  last_edge_ms = now;

  bool isClosed = (digitalRead(g_pin_cadence) == LOW);

  if (!isClosed) {
    armed_for_count = true;
    return;
  }

  if (!armed_for_count) return;
  armed_for_count = false;

  if (last_rev_ms != 0 && (now - last_rev_ms) < MIN_REV_PERIOD_MS) return;

  last_rev_ms = now;
  total_revs++;
  push_timestamp(now);
}

// ------------------ Class Implementation ------------------

PowerReal::PowerReal(float cc, uint8_t pinCadence, uint8_t pinAdc, ICalibration* calibration)
  : cycle_constant(cc), pin_cadence(pinCadence), pin_adc(pinAdc), cal(calibration) {
    g_pin_cadence = pinCadence;
}

void PowerReal::begin() {
  pinMode(pin_cadence, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pin_cadence), cadenceISR, CHANGE);
  Serial.printf("Cadence pin %d initialized\n", pin_cadence);
}

float PowerReal::readAdcRaw() {
  // Quick multi-sample read for noise reduction, returns float for precision
  float sum = 0.0f;
  for (uint8_t i = 0; i < ADC_SAMPLES; i++) {
    sum += (float)analogRead(pin_adc);
  }
  yield();  // Let other tasks run
  return sum / (float)ADC_SAMPLES;
}

void PowerReal::sampleAdc(uint32_t now_ms) {
  // Sample at 3Hz (every 333ms)
  if (now_ms - last_adc_sample_ms < ADC_SAMPLE_INTERVAL_MS) return;
  last_adc_sample_ms = now_ms;

  // Read ADC and add to ring buffer
  float value = readAdcRaw();
  adcBuffer[adcBufferHead] = value;
  adcBufferHead = (adcBufferHead + 1) % ADC_BUF_SIZE;
  if (adcBufferCount < ADC_BUF_SIZE) adcBufferCount++;
}

float PowerReal::getSmoothedAdc(uint32_t now_ms) {
  (void)now_ms;  // Not needed anymore
  if (adcBufferCount == 0) return 0.0f;

  // Simple average of buffer (3 samples = 1 second)
  float sum = 0.0f;
  for (uint8_t i = 0; i < adcBufferCount; i++) {
    sum += adcBuffer[i];
  }
  return sum / (float)adcBufferCount;
}

float PowerReal::readKp(float& rawAdc, uint32_t now_ms) {
  rawAdc = getSmoothedAdc(now_ms);
  return cal->adcToKp(rawAdc);
}

struct Snapshot {
  uint32_t ts[TS_BUF_SIZE];
  uint8_t count;
  uint32_t last_rev;
  uint32_t total_revs;
};

static Snapshot snapshotState() {
  Snapshot s{};
  noInterrupts();
  s.count = ts_count;
  uint8_t head = ts_head;
  for (uint8_t i = 0; i < ts_count; i++) {
    uint8_t idx = (head + TS_BUF_SIZE - ts_count + i) % TS_BUF_SIZE;
    s.ts[i] = ts_buf[idx];
  }
  s.last_rev = last_rev_ms;
  s.total_revs = total_revs;
  interrupts();
  return s;
}

float PowerReal::calculateRpmWindow(uint32_t windowMs) {
  Snapshot s = snapshotState();
  uint32_t now = millis();

  // Timeout check - no revs for a while means stopped
  if (s.last_rev && (now - s.last_rev) > TIMEOUT_MS) {
    return 0.0f;
  }

  if (s.count < 2) return 0.0f;

  uint32_t newest = s.ts[s.count - 1];
  uint8_t first = 0;
  while (first < s.count && (newest - s.ts[first]) > windowMs) first++;

  uint8_t n = s.count - first;
  if (n < 2) return 0.0f;

  uint32_t dt = newest - s.ts[first];
  if (dt == 0) return 0.0f;

  return (float)(n - 1) * 60000.0f / (float)dt;
}

float PowerReal::calculateSmoothedRpm() {
  // Get raw RPM from both windows
  float rpm3s = calculateRpmWindow(WINDOW_SHORT_MS);
  float rpm10s = calculateRpmWindow(WINDOW_LONG_MS);

  // Handle timeout/stopped case
  Snapshot s = snapshotState();
  uint32_t now = millis();
  uint32_t timeSinceLastRev = s.last_rev ? (now - s.last_rev) : TIMEOUT_MS;

  // If completely stopped (timeout), decay smoothly to zero
  if (timeSinceLastRev >= TIMEOUT_MS) {
    // Exponential decay towards zero
    lastSmoothedRpm *= 0.7f;  // Decay factor per update (at 1Hz)
    if (lastSmoothedRpm < 1.0f) lastSmoothedRpm = 0.0f;
    return lastSmoothedRpm;
  }

  // If slowing down (no recent revs but not timed out), use longer window
  // This creates smooth roll-down effect
  if (timeSinceLastRev > 2000) {
    // Blend towards 10s reading as we slow down
    float blendFactor = (float)(timeSinceLastRev - 2000) / 3000.0f;  // 0 to 1 over 3s
    if (blendFactor > 1.0f) blendFactor = 1.0f;
    float blendedRpm = rpm3s * (1.0f - blendFactor) + rpm10s * blendFactor;

    // Also apply decay if current reading is lower than last
    if (blendedRpm < lastSmoothedRpm) {
      // Smooth decay: move 30% towards new value
      lastSmoothedRpm = lastSmoothedRpm * 0.7f + blendedRpm * 0.3f;
    } else {
      lastSmoothedRpm = blendedRpm;
    }
    return lastSmoothedRpm;
  }

  // Normal operation: quick to rise, slow to fall
  float instantRpm = rpm3s;  // Use responsive 3s window as base

  if (instantRpm >= lastSmoothedRpm) {
    // Increasing or stable: respond quickly
    // Use 3s window directly, but smooth slightly to avoid jitter
    lastSmoothedRpm = lastSmoothedRpm * 0.3f + instantRpm * 0.7f;
  } else {
    // Decreasing: use weighted blend favoring longer window
    // This creates the smooth roll-down effect
    float targetRpm = rpm3s * 0.3f + rpm10s * 0.7f;
    // Move 40% towards target (slower response when decreasing)
    lastSmoothedRpm = lastSmoothedRpm * 0.6f + targetRpm * 0.4f;
  }

  return lastSmoothedRpm;
}

void PowerReal::update(uint32_t now_ms) {
  // Continuously sample ADC for 3-second smoothing
  sampleAdc(now_ms);

  // Produce power sample at 1Hz for BLE
  if (now_ms - last_update_ms < 1000) return;
  last_update_ms = now_ms;

  float rpm = calculateSmoothedRpm();
  float rawAdc = 0.0f;
  float kp = readKp(rawAdc, now_ms);
  float power_brake = kp * rpm;
  float power = power_brake * cycle_constant;

  // Update sample
  sample.rpm = rpm;
  sample.kp = kp;
  sample.power_w = power;
  sample.adc_raw = rawAdc;
  
  // Get latest rev counts for BLE
  Snapshot s = snapshotState();
  sample.crank_revs = (uint16_t)s.total_revs;
  
  // Convert last event time to 1/1024s units
  // We need to handle the wrap-around of millis() if running for 49 days, 
  // but for simple casting it might be okay. 
  // BLE expects a free running timer.
  sample.crank_evt_1024 = (uint16_t)((s.last_rev * 1024) / 1000);

  ready = true;
}

bool PowerReal::hasSample() const {
  return ready;
}

PowerSample PowerReal::getSample() {
  ready = false;
  return sample;
}
