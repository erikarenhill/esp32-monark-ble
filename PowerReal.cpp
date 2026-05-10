#include "PowerReal.h"

// ------------------ Configuration ------------------
// Number of samples for single ADC read (noise reduction)
static const uint8_t ADC_SAMPLES = 8;

// Voltage divider configuration (for raw ADC calculation)
static const float R1_KOHM = 4.7f;   // Top resistor (to Vin)
static const float R2_KOHM = 10.0f;  // Bottom resistor (to GND)
static const float DIVIDER_RATIO = (R1_KOHM + R2_KOHM) / R2_KOHM;  // 1.47
static const float ADC_VREF_MV = 3300.0f;  // ESP32 reference voltage
static const float ADC_MAX_RAW = 4095.0f;  // 12-bit ADC

// Cadence tuning
//
// EDGE_DEBOUNCE_MS — minimum gap between two same-direction edges. Reed-switch
// contact bounce is typically 0.5–2 ms; 4 ms is comfortably above that without
// rejecting a legitimate fast contact at the bottom of a hard sprint.
//
// MIN_REV_PERIOD_MS — minimum gap between rev counts. At 100 ms this caps the
// counter at 600 rpm — unreachable in practice — while still rejecting any
// bounce-pair where one closure was visibly wider than EDGE_DEBOUNCE_MS.
//
// Lowering both values gives us a *second chance* to catch a real edge that
// was masked by a previous bounce. The missed-rev back-fill in the ISR is the
// safety net; this tune is about feeding it fewer holes to fill.
static const uint32_t EDGE_DEBOUNCE_MS   = 4;
static const uint32_t MIN_REV_PERIOD_MS  = 100;
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

static volatile uint32_t last_falling_ms = 0;  // Debounce for falling edges (switch closing)
static volatile uint32_t last_rising_ms = 0;   // Debounce for rising edges (switch opening)
static volatile uint32_t last_rev_ms = 0;
static volatile bool armed_for_count = true;
static volatile uint32_t total_revs = 0; // Cumulative revolutions
static volatile uint32_t isr_calls = 0;  // Debug: count ISR calls

static inline void push_timestamp(uint32_t t_ms) {
  ts_buf[ts_head] = t_ms;
  ts_head = (ts_head + 1) % TS_BUF_SIZE;
  if (ts_count < TS_BUF_SIZE) ts_count++;
}

// Average period across the last N revs in the ring, in milliseconds. Returns 0
// if we don't have enough history yet. Cheap integer-only — safe in ISR.
static inline uint32_t avg_recent_period_ms(uint8_t lookback) {
  if (ts_count < lookback + 1) return 0;
  uint32_t sum = 0;
  uint8_t n = 0;
  for (uint8_t k = 0; k < lookback; k++) {
    uint8_t i = (ts_head + TS_BUF_SIZE - 1 - k) % TS_BUF_SIZE;
    uint8_t j = (ts_head + TS_BUF_SIZE - 2 - k) % TS_BUF_SIZE;
    uint32_t a = ts_buf[i];
    uint32_t b = ts_buf[j];
    if (a > b) {
      sum += (a - b);
      n++;
    }
  }
  return n ? (sum / n) : 0;
}

// ISR - debounce rising and falling edges independently. Detects a missed rev
// (a single dropped reed contact at high cadence) by comparing the current
// period against the rolling average of the last 4 — if it's notably longer
// than the average AND we have enough history, we synthesize a virtual rev at
// the midpoint of the gap so cadence stays smooth instead of momentarily
// halving.
void IRAM_ATTR cadenceISR() {
  isr_calls++;  // Debug: count every ISR call
  uint32_t now = millis();
  bool isClosed = (digitalRead(g_pin_cadence) == LOW);

  if (isClosed) {
    // Falling edge (switch closing) - debounce independently
    if ((now - last_falling_ms) < EDGE_DEBOUNCE_MS) return;
    last_falling_ms = now;

    if (!armed_for_count) return;
    armed_for_count = false;

    if (last_rev_ms != 0 && (now - last_rev_ms) < MIN_REV_PERIOD_MS) return;

    // Missed-rev recovery: if this gap is ~2× the recent rolling average,
    // assume the reed missed one closure and back-fill a virtual rev at the
    // midpoint. Bounded above (≤ 4× avg) so a real long pause / stop doesn't
    // get back-filled with phantom revs.
    if (last_rev_ms != 0 && ts_count >= 5) {
      uint32_t avg_p = avg_recent_period_ms(4);
      if (avg_p > 0) {
        uint32_t this_p = now - last_rev_ms;
        // 1.5× threshold: more aggressive — catches near-miss drops where a
        // single contact was lost. 4× upper bound still rejects real pauses
        // (e.g. user slowing down or stopping).
        if (this_p > (avg_p * 15) / 10 && this_p < avg_p * 4) {
          uint32_t virt_t = last_rev_ms + (this_p / 2);
          push_timestamp(virt_t);
          total_revs++;
        }
      }
    }

    last_rev_ms = now;
    total_revs++;
    push_timestamp(now);
  } else {
    // Rising edge (switch opening) - debounce independently
    if ((now - last_rising_ms) < EDGE_DEBOUNCE_MS) return;
    last_rising_ms = now;

    armed_for_count = true;
  }
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

  // Configure ADC for potentiometer reading
  analogSetPinAttenuation(pin_adc, ADC_11db);  // Full range ~0-2.6V
  Serial.printf("ADC pin %d configured with 11dB attenuation\n", pin_adc);

  // Seed EMA with a single read so the first BLE sample isn't 0.
  adcEma = readAdcRaw();
  adcEmaReady = true;
  Serial.printf("ADC EMA seeded at %.0f\n", adcEma);
}

float PowerReal::readAdcRaw() {
  // Quick multi-sample read for noise reduction, returns calibrated millivolts
  // Discard first 2 readings to avoid spikes from ADC settling
  analogReadMilliVolts(pin_adc);
  analogReadMilliVolts(pin_adc);

  float sum = 0.0f;
  for (uint8_t i = 0; i < ADC_SAMPLES; i++) {
    sum += (float)analogReadMilliVolts(pin_adc);
  }
  yield();  // Let other tasks run
  return sum / (float)ADC_SAMPLES;
}

/* static */ float PowerReal::millivoltsToRawAdc(float mv) {
  // Convert measured millivolts back to equivalent raw ADC value (0-4095)
  // accounting for voltage divider: Vin = Vout * (R1+R2)/R2
  float vin_mv = mv * DIVIDER_RATIO;
  return (vin_mv / ADC_VREF_MV) * ADC_MAX_RAW;
}

/* static */ float PowerReal::rawAdcToMillivolts(float raw) {
  // Convert raw ADC value (0-4095) to millivolts at ADC pin
  // accounting for voltage divider: Vout = Vin * R2/(R1+R2)
  float vin_mv = (raw / ADC_MAX_RAW) * ADC_VREF_MV;
  return vin_mv / DIVIDER_RATIO;
}

void PowerReal::sampleAdc(uint32_t now_ms) {
  // 10Hz raw sampling, fed into an EMA. ALPHA=0.333 ≈ 5-sample equivalent
  // (≈500 ms time constant) — half the lag of the old 10-sample ring buffer.
  if (now_ms - last_adc_sample_ms < ADC_SAMPLE_INTERVAL_MS) return;
  last_adc_sample_ms = now_ms;

  float value = readAdcRaw();
  if (!adcEmaReady) {
    adcEma = value;
    adcEmaReady = true;
  } else {
    adcEma = ADC_EMA_ALPHA * value + (1.0f - ADC_EMA_ALPHA) * adcEma;
  }
}

float PowerReal::getSmoothedAdc(uint32_t now_ms) {
  (void)now_ms;
  return adcEmaReady ? adcEma : 0.0f;
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

  // Outlier rejection: reject impossibly fast jumps (>1.5x previous) once
  // we're in steady pedalling. Source TCX comparison showed isolated 156 rpm
  // spikes from double-counted pulses — they'd otherwise smear into the BLE
  // power output via power = brake * rpm * cc.
  // Guard with lastSmoothedRpm > 30 so a real start-from-stop rampup
  // (0 -> 80 rpm) isn't rejected.
  if (lastSmoothedRpm > 30.0f && instantRpm > lastSmoothedRpm * 1.5f) {
    return lastSmoothedRpm;  // hold the previous value for this cycle
  }

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
  
  // Convert last event time to 1/1024s units (CPS spec — wraps every 64s, that's fine).
  // Use 64-bit arithmetic so the intermediate (ms * 1024) doesn't overflow uint32_t
  // after ~70 minutes of uptime.
  sample.crank_evt_1024 = (uint16_t)(((uint64_t)s.last_rev * 1024) / 1000);

  ready = true;
}

bool PowerReal::hasSample() const {
  return ready;
}

PowerSample PowerReal::getSample() {
  ready = false;
  return sample;
}
