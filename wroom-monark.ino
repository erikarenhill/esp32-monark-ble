// Display Selection
#define DISPLAY_TYPE_LCD1602 1
#define DISPLAY_TYPE_TFT     2
#define DISPLAY_TO_USE DISPLAY_TYPE_TFT

#include <Arduino.h>
#include "PowerSource.h"
#include "PowerSimulator.h"
#include "PowerReal.h"
#include "BleCps.h"
//#ifdef DISPLAY_TO_USE == DISPLAY_TYPE_LCD1602
  #include "LcdUi1602.h"
//#elif DISPLAY_TO_USE == DISPLAY_TYPE_TFT
  #include "TftUi.h"

//#endif
#include "SettingsManager.h"
#include "CalibrationProcess.h"

#include "BoardConfig.h"

// -------- CONFIG --------
static const bool USE_SIMULATOR = true;
static const float CYCLE_CONSTANT = 1.05f;
static const bool DEVELOPER_MODE = true; // If true, skip auto-calibration on missing settings


static const int DISPLAY_TYPE = DISPLAY_TO_USE; // Change to DISPLAY_TYPE_TFT for new display

// Default / Developer Calibration Values
static const int CAL_DEF_0KP = 78;
static const int CAL_DEF_2KP = 125;
static const int CAL_DEF_4KP = 177;
static const int CAL_DEF_6KP = 226;

// Pins are now defined in BoardConfig.h
// I2C_SDA_PIN, I2C_SCL_PIN, CADENCE_PIN, ADC_PIN, LCD_ADDR, CAL_BUTTON_PIN

// -------- Objects --------
PowerSource* power = nullptr;
BleCps ble;
IDisplay* display = nullptr;
MonarkCalibration* calibration = nullptr; // Changed to concrete type to allow update
SettingsManager settings;
CalibrationProcess* calProcess = nullptr;

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("--- Monark ESP32 ---");
  Serial.printf("Cadence Pin: %d\n", CADENCE_PIN);
  Serial.printf("ADC Pin: %d\n", ADC_PIN);
  Serial.printf("Cal Button Pin: %d\n", CAL_BUTTON_PIN);

  // Settings
  settings.begin();

  // Display
  if (DISPLAY_TYPE == DISPLAY_TYPE_LCD1602) {
    display = new LcdUi1602(LCD_ADDR, 16, 2, I2C_SDA_PIN, I2C_SCL_PIN);
  } else {
    display = new TftUi();
  }
  display->begin();

  // Load calibration (needed for both sim and real)
  int a0=CAL_DEF_0KP, a2=CAL_DEF_2KP, a4=CAL_DEF_4KP, a6=CAL_DEF_6KP;
  bool loaded = settings.loadCalibration(a0, a2, a4, a6);

  if (loaded) {
    Serial.println("Loaded calibration from NVS.");
  } else {
    Serial.println("Using default/developer calibration.");
  }
  Serial.printf("Cal: %d %d %d %d\n", a0, a2, a4, a6);

  calibration = new MonarkCalibration(a0, a2, a4, a6);

  // Power source (sim or real)
  if (USE_SIMULATOR) {
    power = new PowerSimulator(CYCLE_CONSTANT);
  } else {
    power = new PowerReal(CYCLE_CONSTANT, CADENCE_PIN, ADC_PIN, calibration);
  }
  power->begin();

  // Init calibration process (available in both modes)
  calProcess = new CalibrationProcess(CAL_BUTTON_PIN, ADC_PIN, display, &settings, calibration);
  calProcess->begin();

  if (!loaded && !DEVELOPER_MODE && !USE_SIMULATOR) {
    Serial.println("No settings found. Starting calibration...");
    calProcess->startCalibration();
  }

  // BLE CPS transport
  ble.begin("LT2-PowerSim");

  Serial.println("System started (LCD + BLE).");
}

void loop() {
  uint32_t now = millis();

  // Handle calibration (it manages its own touch input)
  if (calProcess && calProcess->isCalibrating()) {
    calProcess->update();
    power->update(now);
    if (power->hasSample()) {
      PowerSample s = power->getSample();
      ble.notify(s);
    }
    delay(10);
    return;
  }

  // Normal mode: update display input (touch)
  if (display) display->update();

  // Check for manual calibration request from UI
  if (display && display->isActionRequested()) {
    if (calProcess) {
      calProcess->startCalibration();
    }
  }

  // Update calibration process (for non-calibrating state)
  if (calProcess) {
    calProcess->update();
  }

  power->update(now);

  if (power->hasSample()) {
    PowerSample s = power->getSample();

    // Serial debug
    Serial.printf("rpm=%.1f kp=%.2f P=%.1fW rev=%u evt=%u adc=%u\n",
                  s.rpm, s.kp, s.power_w, s.crank_revs, s.crank_evt_1024, s.adc_raw);

    // LCD
    display->showPower(s, CYCLE_CONSTANT);

    // BLE
    ble.notify(s);
  }

  delay(1);
}
