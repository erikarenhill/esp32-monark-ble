#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "IDisplay.h"

class LcdUi1602 : public IDisplay {
public:
  // addr: usually 0x27 or 0x3F
  LcdUi1602(uint8_t addr, uint8_t cols, uint8_t rows, int sda, int scl);

  // IDisplay implementation
  void begin() override;
  void showPower(const PowerSample& s, const WorkoutDisplay* workout = nullptr) override;
  void showMessage(const char* line1, const char* line2) override;

private:
  LiquidCrystal_I2C lcd;
  uint8_t cols;
  uint8_t rows;
  int _sda;
  int _scl;

  void printFixed(uint8_t col, uint8_t row, const char* text);
};
