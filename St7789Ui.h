#pragma once
#include <Arduino.h>
#include "IDisplay.h"

// Forward declares so the heavy graphics headers don't leak through.
class Arduino_DataBus;
class Arduino_GFX;

// Minimal IDisplay implementation for the Waveshare ESP32-C6-LCD-1.47
// (ST7789 driver, 172x320 panel, column offset 34, BGR + inversion ON).
class St7789Ui : public IDisplay {
public:
  St7789Ui();
  ~St7789Ui() override;

  void begin() override;
  void showPower(const PowerSample& s, const WorkoutDisplay* workout = nullptr) override;
  void showMessage(const char* line1, const char* line2) override;
  void setStatus(const char* text) override;

private:
  Arduino_DataBus* _bus = nullptr;
  Arduino_GFX* _gfx = nullptr;
  bool _ready = false;

  // Cache last-drawn values so we only repaint what changed (avoids flicker).
  float _lastPower = -9999.0f;
  float _lastRpm = -1.0f;
  uint32_t _lastFullRedrawMs = 0;

  // Small status text painted above the POWER label (e.g. WiFi IP).
  char _status[32] = {0};

  void drawStaticChrome();
  void drawStatusBar();
  void drawNumber(int x, int y, int w, int h, uint8_t textSize, const char* text);
};
