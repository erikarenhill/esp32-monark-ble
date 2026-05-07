#pragma once
#include <Arduino.h>
#include "IDisplay.h"

// Forward declares so the heavy graphics headers don't leak through.
class Arduino_DataBus;
class Arduino_GFX;

// Minimal IDisplay implementation for the Waveshare ESP32-C6-LCD-1.47
// (ST7789 driver, 172x320 panel, column offset 34, BGR + inversion ON).
//
// Theme: Cinder — industrial / mechanical. Warm dark charcoal background,
// bone-coloured ink, dim-grey labels, single amber accent. Stacked POWER and
// CADENCE blocks with calibrated tickmark visuals; small status footer at the
// bottom carries the WiFi IP in the accent colour.
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

  // Status text painted above the POWER block (e.g. WiFi IP).
  char _status[32] = {0};

  // Recent-power tick bar (Cinder hallmark). 24 entries, newest on the right.
  static constexpr uint8_t HIST_N = 24;
  float _powerHist[HIST_N] = {0};
  uint8_t _powerHistHead = 0;
  uint8_t _powerHistCount = 0;

  // Running stats (since boot) for the AVG / MAX line.
  float _powerSum = 0.0f;
  uint32_t _powerSamples = 0;
  float _powerMax = 0.0f;
  // Cached strings so we only repaint the small AVG/MAX line when a value flips.
  int _lastAvgInt = -1;
  int _lastMaxInt = -1;

  void drawStaticChrome();
  void drawStatusBar();
  void drawTickBar();
  void drawAvgMaxLine();
  void drawCadenceTargetBand();
  void drawNumber(int x, int y, int w, int h, uint8_t textSize, const char* text);
  void pushPowerHistory(float p);
};
