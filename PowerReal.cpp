#include "PowerReal.h"

// ------------------ Configuration ------------------
// ADC Calibration (from reference code)
static const int ADC_0KP = 78;
static const int ADC_2KP = 125;
static const int ADC_4KP = 177;
static const int ADC_6KP = 226;

static const uint8_t ADC_SAMPLES = 32;

// Cadence tuning
static const uint32_t EDGE_DEBOUNCE_MS   = 12;
static const uint32_t MIN_REV_PERIOD_MS  = 200;
static const uint32_t WINDOW_MS          = 3000;   // 3 s smoothing
static const uint32_t TIMEOUT_MS         = 5000;

// ------------------ ISR & Globals ------------------
// We use static/global variables for the ISR because attaching a class member is complex
static volatile uint8_t  g_pin_cadence = 0;

static const uint8_t TS_BUF_SIZE = 32;
static volatile uint32_t ts_buf[TS_BUF_SIZE];
static volatile uint8_t  ts_head = 0;
static volatile uint8_t  ts_count = 0;

static volatile uint32_t last_edge_ms = 0;
static volatile uint32_t last_rev_ms = 0;
static volatile bool armed_for_count = true;
static volatile uint32_t total_revs = 0; // Cumulative revolutions

static inline void push_timestamp(uint32_t t_ms) {
  ts_buf[ts_head] = t_ms;
  ts_head = (ts_head + 1) % TS_BUF_SIZE;
  if (ts_count < TS_BUF_SIZE) ts_count++;
}

// ISR
void IRAM_ATTR cadenceISR() {
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
  
  // ESP32 ADC setup if needed (usually analogRead handles it, but we might want attenuation)
  // analogSetAttenuation(ADC_11db); // Default is usually fine, but depends on voltage range
}

float PowerReal::readKp(uint16_t& rawAdc) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < ADC_SAMPLES; i++) {
    sum += analogRead(pin_adc);
    delay(1); // small delay
  }
  int avg = sum / ADC_SAMPLES;
  rawAdc = (uint16_t)avg;
  return cal->adcToKp(avg);
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

float PowerReal::calculateRpm() {
  Snapshot s = snapshotState();
  uint32_t now = millis();

  // Timeout check
  if (s.last_rev && (now - s.last_rev) > TIMEOUT_MS) {
    return 0.0f;
  }

  if (s.count < 2) return 0.0f;

  uint32_t newest = s.ts[s.count - 1];
  uint8_t first = 0;
  while (first < s.count && (newest - s.ts[first]) > WINDOW_MS) first++;

  uint8_t n = s.count - first;
  if (n < 2) return 0.0f;

  uint32_t dt = newest - s.ts[first];
  if (dt == 0) return 0.0f;

  return (float)(n - 1) * 60000.0f / (float)dt;
}

void PowerReal::update(uint32_t now_ms) {
  // Update at 1Hz roughly, or faster? 
  // The main loop calls this frequently. We should probably update internal state 
  // but only produce a "sample" periodically or when data changes.
  // For BLE, 1Hz is standard.
  
  if (now_ms - last_update_ms < 1000) return;
  last_update_ms = now_ms;

  float rpm = calculateRpm();
  uint16_t rawAdc = 0;
  float kp = readKp(rawAdc);
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
