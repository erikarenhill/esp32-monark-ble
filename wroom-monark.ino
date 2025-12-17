// Display Selection
#define DISPLAY_TYPE_LCD1602 1
#define DISPLAY_TYPE_TFT     2
#define DISPLAY_TO_USE 0

#include <Arduino.h>
#include "PowerSource.h"
#include "PowerSimulator.h"
#include "PowerReal.h"
#include "BleCps.h"
//#include "LcdUi1602.h"
//#include "TftUi.h"
//#include "Menu.h"
#include "Workout.h"
#include "SettingsManager.h"
#include "CalibrationProcess.h"
#include "PowerWebServer.h"

#include "BoardConfig.h"

// -------- CONFIG --------
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
MonarkCalibration* calibration = nullptr;
SettingsManager settings;
CalibrationProcess* calProcess = nullptr;
Workout workout;
PowerWebServer* webServer = nullptr;

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("--- Monark ESP32 ---");
  Serial.printf("Cadence Pin: %d\n", CADENCE_PIN);
  Serial.printf("ADC Pin: %d\n", ADC_PIN);
  Serial.printf("Cal Button Pin: %d\n", CAL_BUTTON_PIN);
  Serial.flush();

  // Settings
  Serial.println("Init settings...");
  Serial.flush();
  settings.begin();
  Serial.println("Settings OK");
  Serial.flush();

  // Display
  if (DISPLAY_TYPE == DISPLAY_TYPE_LCD1602) {
  //  display = new LcdUi1602(LCD_ADDR, 16, 2, I2C_SDA_PIN, I2C_SCL_PIN);
  } else {
    //display = new TftUi();
  }
  if (display) {
    display->begin();
  }
  Serial.println("Display OK (null)");
  Serial.flush();

  // Load calibration (needed for both sim and real)
  Serial.println("Loading calibration...");
  Serial.flush();
  int a0=CAL_DEF_0KP, a2=CAL_DEF_2KP, a4=CAL_DEF_4KP, a6=CAL_DEF_6KP;
  bool loaded = settings.loadCalibration(a0, a2, a4, a6);

  if (loaded) {
    Serial.println("Loaded calibration from NVS.");
  } else {
    Serial.println("Using default/developer calibration.");
  }
  Serial.printf("Cal: %d %d %d %d\n", a0, a2, a4, a6);

  calibration = new MonarkCalibration(a0, a2, a4, a6);

  // Load cycle constant from settings
  float cycleConstant = settings.loadCycleConstant(CYCLE_CONSTANT);
  Serial.printf("Cycle constant: %.2f\n", cycleConstant);

  // Load simulator mode from settings (defaults to false)
  bool useSimulator = settings.loadSimulatorMode(false);
  Serial.printf("Simulator mode: %s\n", useSimulator ? "ON" : "OFF");

  // Power source (sim or real)
  if (useSimulator) {
    power = new PowerSimulator(cycleConstant);
  } else {
    power = new PowerReal(cycleConstant, CADENCE_PIN, ADC_PIN, calibration);
  }
  power->begin();

  // Init calibration process (available in both modes)
  calProcess = new CalibrationProcess(CAL_BUTTON_PIN, ADC_PIN, display, &settings, calibration);
  calProcess->begin();

  if (!loaded && !DEVELOPER_MODE && !useSimulator) {
    Serial.println("No settings found. Starting calibration...");
    calProcess->startCalibration();
  }

  // Load device name for BLE and WiFi
  String deviceName = settings.loadDeviceName("MonarkPower");
  Serial.printf("Device name: %s\n", deviceName.c_str());
  Serial.flush();

  // BLE CPS transport (uses device name)
  Serial.println("Starting BLE...");
  Serial.flush();
  ble.begin(deviceName.c_str());
  Serial.println("BLE OK");
  Serial.flush();

  // Web server for power data and calibration (uses device name for WiFi AP)
  Serial.println("Starting WiFi...");
  Serial.flush();
  webServer = new PowerWebServer(&settings, calibration, ADC_PIN);
  webServer->begin();  // Uses device name from settings
  Serial.println("WiFi OK");
  Serial.flush();

  Serial.println("System started (LCD + BLE + WiFi).");
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
 /* if (display) {
    display->update();

    // Handle menu actions
    Menu* menu = display->getMenu();
    if (menu) {
      MenuAction action = menu->getLastAction();
      if (action == MenuAction::Calibration) {
        if (calProcess) {
          calProcess->startCalibration();
        }
      }
      else if (action == MenuAction::ViewCalibration) {
        if (calibration) {
          char line1[32], line2[32];
          snprintf(line1, sizeof(line1), "0kp:%d 2kp:%d",
                   calibration->getAdc0(), calibration->getAdc2());
          snprintf(line2, sizeof(line2), "4kp:%d 6kp:%d",
                   calibration->getAdc4(), calibration->getAdc6());
          display->showMessage(line1, line2);
        }
      }
    }

    // Handle workout actions
    MenuAction workoutAction = display->getWorkoutAction();
    switch (workoutAction) {
      case MenuAction::Start:
        workout.start();
        Serial.println("Workout started");
        break;
      case MenuAction::Pause:
        if (workout.isRunning()) {
          workout.pause();
          Serial.println("Workout paused");
        } else if (workout.isPaused()) {
          workout.resume();
          Serial.println("Workout resumed");
        }
        break;
      case MenuAction::Stop:
        workout.stop();
        Serial.println("Workout stopped");
        break;
      case MenuAction::Lap:
        if (workout.isRunning()) {
          workout.lap();
          Serial.printf("Lap %d recorded\n", workout.getLapCount());
        }
        break;
      default:
        break;
    }

    // isActionRequested handles NEXT button in calibration
    if (display->isActionRequested()) {
      if (!menu && calProcess) {
        calProcess->startCalibration();
      }
    }
  }*/

  // Update calibration process (for non-calibrating state)
  if (calProcess) {
    calProcess->update();
  }

  power->update(now);

  if (power->hasSample()) {
    PowerSample s = power->getSample();

    // Add power sample to workout if running
    if (workout.isRunning()) {
      workout.addPowerSample(s.power_w);
    }

    // Serial debug
    //Serial.printf("rpm=%.1f kp=%.2f P=%.1fW rev=%u evt=%u adc=%.2f\n",  s.rpm, s.kp, s.power_w, s.crank_revs, s.crank_evt_1024, s.adc_raw);

    // Prepare workout display info
    WorkoutDisplay wd;
    wd.active = !workout.isStopped();
    wd.running = workout.isRunning();
    wd.paused = workout.isPaused();
    wd.elapsedMs = workout.getElapsedMs();
    wd.lapMs = workout.getCurrentLapMs();
    wd.avgPower = workout.getTotalAvgPower();
    wd.lapAvgPower = workout.getCurrentLapAvgPower();
    wd.lapNumber = workout.getLapCount();

    // Update display (skip if menu is open)
    if (display && !display->isMenuOpen()) {
      display->showPower(s, &wd);
    }

    // BLE
    ble.notify(s);

    // Web server
    if (webServer) {
      webServer->updatePowerData(s);
    }
  }

  // Yield to async web server and other tasks
  yield();
  delay(1);
}
