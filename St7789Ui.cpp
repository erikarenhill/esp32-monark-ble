#include "St7789Ui.h"
#include <Arduino_GFX_Library.h>

// Pins for the Waveshare ESP32-C6-LCD-1.47 (fixed by the board).
static constexpr int8_t PIN_DC   = 15;
static constexpr int8_t PIN_CS   = 14;
static constexpr int8_t PIN_SCK  = 7;
static constexpr int8_t PIN_MOSI = 6;
static constexpr int8_t PIN_RST  = 21;
static constexpr int8_t PIN_BL   = 22;

// Panel: 172x320, column offset 34 (since the ST7789 driver chip RAM is 240 wide
// and the visible 172px is centered at 34..205). BGR + inversion ON.
static constexpr int16_t PANEL_W = 172;
static constexpr int16_t PANEL_H = 320;
static constexpr int16_t COL_OFFSET = 34;

// Colors (RGB565). Note BGR panels swap R/B at hardware level — Arduino_GFX
// handles that automatically when we tell it the panel is BGR via setOrder.
static constexpr uint16_t COL_BG     = 0x0000; // black
static constexpr uint16_t COL_FG     = 0xFFFF; // white
static constexpr uint16_t COL_LABEL  = 0x7BEF; // grey
static constexpr uint16_t COL_POWER  = 0x07FF; // cyan-ish
static constexpr uint16_t COL_RPM    = 0xFFE0; // yellow
static constexpr uint16_t COL_STATUS = 0x528A; // dim grey (so the IP doesn't fight POWER for attention)

// Backlight PWM. 180/255 ≈ 70% — comfortable indoors, still readable. Bump if
// the bike is in direct sun.
static constexpr uint32_t BL_FREQ_HZ  = 5000;
static constexpr uint8_t  BL_RES_BITS = 8;
static constexpr uint8_t  BL_DUTY     = 180;

St7789Ui::St7789Ui() = default;

St7789Ui::~St7789Ui() {
  delete _gfx;
  delete _bus;
}

void St7789Ui::begin() {
  // Backlight via LEDC PWM so we can run it dimmer than full-bright. Attaching
  // also implicitly puts the pin in OUTPUT mode; start at 0 (off) until the
  // panel is initialised, then ramp to BL_DUTY.
  ledcAttach(PIN_BL, BL_FREQ_HZ, BL_RES_BITS);
  ledcWrite(PIN_BL, 0);

  _bus = new Arduino_ESP32SPI(
      PIN_DC, PIN_CS, PIN_SCK, PIN_MOSI, GFX_NOT_DEFINED /* MISO */);

  // Arduino_ST7789 ctor:
  //   bus, rst, rotation, IPS, w, h, col_off1, row_off1, col_off2, row_off2
  // For this 172x320 panel, both passes use the same column offset of 34.
  _gfx = new Arduino_ST7789(
      _bus, PIN_RST, /*rotation=*/0, /*IPS=*/true,
      PANEL_W, PANEL_H, COL_OFFSET, 0, COL_OFFSET, 0);

  if (!_gfx->begin()) {
    Serial.println("St7789Ui: gfx->begin() failed");
    return;
  }

  _gfx->fillScreen(COL_BG);
  _gfx->invertDisplay(true); // BGR + inversion-on per the panel's setup

  ledcWrite(PIN_BL, BL_DUTY); // backlight at dimmed level
  _ready = true;

  drawStaticChrome();
}

void St7789Ui::drawStatusBar() {
  if (!_ready) return;
  // 8-row band at the very top, redrawn from blank each time so a shorter
  // string (e.g. "AP 192.168.4.1" → "10.0.0.5") doesn't leave trailing chars.
  _gfx->fillRect(0, 0, PANEL_W, 10, COL_BG);
  if (_status[0] == '\0') return;
  _gfx->setTextColor(COL_STATUS, COL_BG);
  _gfx->setTextSize(1);
  _gfx->setCursor(4, 1);
  _gfx->print(_status);
}

void St7789Ui::drawStaticChrome() {
  if (!_ready) return;

  _gfx->setTextWrap(false);

  drawStatusBar();

  // POWER label — pushed down 8px to make room for the status bar above.
  _gfx->setCursor(8, 14);
  _gfx->setTextColor(COL_LABEL, COL_BG);
  _gfx->setTextSize(2);
  _gfx->print("POWER");

  // CADENCE label (lower half)
  _gfx->setCursor(8, 168);
  _gfx->setTextColor(COL_LABEL, COL_BG);
  _gfx->setTextSize(2);
  _gfx->print("CADENCE");

  // Units
  _gfx->setCursor(140, 130);
  _gfx->setTextColor(COL_LABEL, COL_BG);
  _gfx->setTextSize(2);
  _gfx->print("W");

  _gfx->setCursor(110, 290);
  _gfx->setTextColor(COL_LABEL, COL_BG);
  _gfx->setTextSize(2);
  _gfx->print("rpm");
}

// Draw a right-justified number into a fixed-size box, clearing it first
// so we don't get artifacts from a wider previous value.
void St7789Ui::drawNumber(int x, int y, int w, int h, uint8_t textSize, const char* text) {
  _gfx->fillRect(x, y, w, h, COL_BG);
  // Approximate glyph width for the default font: 6 px * textSize per char.
  int charW = 6 * textSize;
  int textW = (int)strlen(text) * charW;
  int tx = x + (w - textW); // right-justified
  if (tx < x) tx = x;
  _gfx->setCursor(tx, y);
  _gfx->setTextSize(textSize);
  _gfx->print(text);
}

void St7789Ui::showPower(const PowerSample& s, const WorkoutDisplay* /*workout*/) {
  if (!_ready) return;

  // Re-assert backlight duty every tick — LEDC keeps it set across resets, but
  // re-writing is cheap and protects against anything that detached the pin.
  ledcWrite(PIN_BL, BL_DUTY);

  // Force a periodic redraw even when values are unchanged, so we never sit
  // on a stale frame if a draw was dropped (e.g., SPI glitch) or the panel
  // briefly went idle. Cheap (one full chrome+number repaint per N ms).
  uint32_t now = millis();
  bool periodic = (now - _lastFullRedrawMs) > 5000;
  if (periodic) {
    _lastFullRedrawMs = now;
    _lastPower = -9999.0f;
    _lastRpm = -1.0f;
    drawStaticChrome();
  }

  if (fabsf(s.power_w - _lastPower) >= 0.5f) {
    _lastPower = s.power_w;
    int p = (int)lroundf(s.power_w);
    if (p < 0) p = 0;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", p);
    _gfx->setTextColor(COL_POWER, COL_BG);
    drawNumber(/*x=*/8, /*y=*/40, /*w=*/156, /*h=*/96, /*size=*/8, buf);
  }

  if (fabsf(s.rpm - _lastRpm) >= 0.5f) {
    _lastRpm = s.rpm;
    int r = (int)lroundf(s.rpm);
    if (r < 0) r = 0;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", r);
    _gfx->setTextColor(COL_RPM, COL_BG);
    drawNumber(/*x=*/8, /*y=*/200, /*w=*/156, /*h=*/72, /*size=*/8, buf);
  }
}

void St7789Ui::showMessage(const char* line1, const char* line2) {
  if (!_ready) return;
  _gfx->fillScreen(COL_BG);
  _gfx->setTextColor(COL_FG, COL_BG);
  _gfx->setTextSize(2);
  if (line1) {
    _gfx->setCursor(8, 60);
    _gfx->print(line1);
  }
  if (line2) {
    _gfx->setCursor(8, 100);
    _gfx->print(line2);
  }
  // Force re-chrome on next showPower
  _lastPower = -9999.0f;
  _lastRpm = -1.0f;
  // Note: caller should follow up with showPower() to repaint chrome.
}

void St7789Ui::setStatus(const char* text) {
  if (text == nullptr) text = "";
  // Truncate to fit the buffer; the bar paints whatever fits at size-1.
  strncpy(_status, text, sizeof(_status) - 1);
  _status[sizeof(_status) - 1] = '\0';
  drawStatusBar();
}
