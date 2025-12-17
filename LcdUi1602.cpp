#include "LcdUi1602.h"

LcdUi1602::LcdUi1602(uint8_t addr, uint8_t cols_, uint8_t rows_, int sda, int scl)
: lcd(addr, cols_, rows_), cols(cols_), rows(rows_), _sda(sda), _scl(scl) {}

void LcdUi1602::begin() {
  Wire.begin(_sda, _scl);
  lcd.begin(cols, rows);   // your LiquidCrystal_I2C lib expects args
  lcd.backlight();
  lcd.clear();

  showMessage("LT2 Power", "Starting...");
}

void LcdUi1602::printFixed(uint8_t col, uint8_t row, const char* text) {
  lcd.setCursor(col, row);
  lcd.print(text);

  int len = strlen(text);
  for (int i = len; i < (int)(cols - col); i++) lcd.print(' ');
}

void LcdUi1602::showMessage(const char* line1, const char* line2) {
  if (line1) printFixed(0, 0, line1);
  if (line2) printFixed(0, 1, line2);
}

void LcdUi1602::showPower(const PowerSample& s, const WorkoutDisplay* workout) {
  char line1[17];
  char line2[17];

  // Line 1: P:####W C:###
  // Line 2: kp:#.## A:####
  snprintf(line1, sizeof(line1), "P:%4.0fW C:%3.0f", s.power_w, s.rpm);
  snprintf(line2, sizeof(line2), "kp:%1.2f A:%4u", s.kp, s.adc_raw);

  showMessage(line1, line2);
}
