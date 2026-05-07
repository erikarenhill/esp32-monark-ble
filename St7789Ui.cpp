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

// ─── Cinder palette ──────────────────────────────────────────────────────────
// Hex tokens from themes.jsx mapped to RGB565. Background uses pure black
// (0x0000) instead of the design's #0e0c0a (0x1041) — on this Waveshare panel
// + Arduino_GFX combo the warm-dark value renders as a visibly bright off-
// white instead of charcoal (a quirk of how invertDisplay interacts with
// non-zero low values). Pure black gives the same visual goal — a
// near-black background — without the colour inversion artefact.
//   bg     #0e0c0a → 0x0000 (rendered as true black)
//   panel  #181613 → 0x1882
//   ink    #f3ece0 → 0xF77C (warm bone)
//   dim    #7a6f5e → 0x7B6B (warm grey)
//   rule   #2a251e → 0x2923 (dark warm)
//   accent #ff8a14 → 0xFC42 (amber primary)
//   accent2#ffb84d → 0xFDC9 (warm highlight)
//   ok     #a3c46a → 0xA62D (muted green)
//   warn   #e85a3c → 0xEACB
static constexpr uint16_t COL_BG     = 0x0000;
static constexpr uint16_t COL_PANEL  = 0x1882;
static constexpr uint16_t COL_INK    = 0xF77C;
static constexpr uint16_t COL_DIM    = 0x7B6B;
static constexpr uint16_t COL_RULE   = 0x2923;
static constexpr uint16_t COL_ACCENT = 0xFC42;
static constexpr uint16_t COL_ACCENT2= 0xFDC9;
static constexpr uint16_t COL_OK     = 0xA62D;

// Backlight PWM. 180/255 ≈ 70% — comfortable indoors, still readable.
static constexpr uint32_t BL_FREQ_HZ  = 5000;
static constexpr uint8_t  BL_RES_BITS = 8;
static constexpr uint8_t  BL_DUTY     = 180;

// ─── Layout (172×320 portrait) ───────────────────────────────────────────────
//
//  0 ┌────────────────────────────────┐   ← top accent rule (1px, accent)
//  4 │ POWER                  3s 247  │   sub-line, size 1
// 18 │ ┌──────────────────────────┐   │
// 22 │ │           238            │   │   big number, size 7 (56 px tall)
// 78 │ └──────────────────────────┘   │
// 84 │ │ ▁▂▂▃▄▅▆▇█▇▆▅ … (tick bar) │   12 px tall
// 96 │ AVG 221      MAX 412           │   size 1
//106 ├────────────────────────────────┤   ← rule
//112 │ CADENCE                   RPM  │   sub-line
//126 │           92                   │   big number, size 7 ends y=182
//182 │ │ tgt-band ─────●─── │           target indicator
//
//296 ├────────────────────────────────┤
//300 │ WEB             192.168.4.1    │  size 2 footer with accent IP
//318 └────────────────────────────────┘

// Layout:
//   y=0..14    tick rule
//   y=22..30   POWER label (size 1) + "3s NNN" sub-line on right
//   y=36..92   POWER number (size 7 = 56 px tall)
//   y=98..110  power history tick mosaic (12 px)
//   y=116..124 AVG / MAX line
//   y=132      mid rule
//   y=138..146 CADENCE label + "RPM" right
//   y=152..208 CADENCE number
//   y=216..220 cadence target band
//   y=298..318 footer (IP)
static constexpr int16_t POW_LABEL_Y = 22;
static constexpr int16_t POW_NUM_Y   = 36;
static constexpr int16_t POW_NUM_H   = 56;        // size 7 cell height
static constexpr int16_t TICK_Y      = 98;
static constexpr int16_t TICK_H      = 12;
static constexpr int16_t AVGMAX_Y    = 116;
static constexpr int16_t RULE_MID_Y  = 132;
static constexpr int16_t CAD_LABEL_Y = 138;
static constexpr int16_t CAD_NUM_Y   = 152;
static constexpr int16_t CAD_NUM_H   = 56;
static constexpr int16_t CAD_BAND_Y  = 216;
static constexpr int16_t CAD_BAND_H  = 4;
static constexpr int16_t FOOTER_Y    = 300;       // size 2 footer

St7789Ui::St7789Ui() = default;

St7789Ui::~St7789Ui() {
  delete _gfx;
  delete _bus;
}

void St7789Ui::begin() {
  // Backlight via LEDC PWM so we can run it dimmer than full-bright.
  ledcAttach(PIN_BL, BL_FREQ_HZ, BL_RES_BITS);
  ledcWrite(PIN_BL, 0);

  _bus = new Arduino_ESP32SPI(
      PIN_DC, PIN_CS, PIN_SCK, PIN_MOSI, GFX_NOT_DEFINED /* MISO */);

  _gfx = new Arduino_ST7789(
      _bus, PIN_RST, /*rotation=*/0, /*IPS=*/true,
      PANEL_W, PANEL_H, COL_OFFSET, 0, COL_OFFSET, 0);

  if (!_gfx->begin()) {
    Serial.println("St7789Ui: gfx->begin() failed");
    return;
  }

  // The Arduino_ST7789 ctor with IPS=true already sends INVON during begin();
  // calling invertDisplay(true) again here re-toggles it to the wrong polarity
  // and the panel renders 0x0000 as bright instead of black. invertDisplay
  // is therefore *not* called explicitly — the ctor handles it.
  _gfx->fillScreen(COL_BG);

  ledcWrite(PIN_BL, BL_DUTY);
  _ready = true;

  drawStaticChrome();
}

// Footer: "WEB" left in dim, IP right in accent. Always 320-px panel-bottom.
void St7789Ui::drawStatusBar() {
  if (!_ready) return;
  // Clear the footer band from y=PANEL_H-22 to bottom.
  _gfx->fillRect(0, PANEL_H - 22, PANEL_W, 22, COL_PANEL);
  _gfx->drawFastHLine(0, PANEL_H - 22, PANEL_W, COL_RULE);

  // "WEB" label left
  _gfx->setTextColor(COL_DIM, COL_PANEL);
  _gfx->setTextSize(1);
  _gfx->setCursor(6, PANEL_H - 14);
  _gfx->print("WEB");

  // IP right-justified at size 1 (so a 15-char ip fits with room to spare).
  if (_status[0] != '\0') {
    int ipW = (int)strlen(_status) * 6;  // glyph cell at size 1
    int x = PANEL_W - ipW - 6;
    if (x < 30) x = 30;
    _gfx->setTextColor(COL_ACCENT, COL_PANEL);
    _gfx->setCursor(x, PANEL_H - 14);
    _gfx->print(_status);
  }
}

void St7789Ui::drawStaticChrome() {
  if (!_ready) return;

  _gfx->setTextWrap(false);

  // Top tick rule (matches the Boot screen's calibrated-feel ruler).
  // 30 vertical ticks, every 5th one taller. Spans the panel width.
  _gfx->fillRect(0, 0, PANEL_W, 14, COL_BG);
  for (int i = 0; i < 30; i++) {
    int x = 6 + (i * (PANEL_W - 12)) / 29;
    int h = (i % 5 == 0) ? 6 : 3;
    uint16_t c = (i % 5 == 0) ? COL_DIM : COL_RULE;
    _gfx->drawFastVLine(x, 14 - h, h, c);
  }
  _gfx->drawFastHLine(0, 14, PANEL_W, COL_RULE);

  // POWER label row
  _gfx->fillRect(0, POW_LABEL_Y, PANEL_W, 8, COL_BG);
  _gfx->setTextColor(COL_DIM, COL_BG);
  _gfx->setTextSize(1);
  _gfx->setCursor(8, POW_LABEL_Y);
  _gfx->print("POWER");

  // Mid-rule between POWER and CADENCE
  _gfx->drawFastHLine(0, RULE_MID_Y, PANEL_W, COL_RULE);

  // CADENCE label row
  _gfx->fillRect(0, CAD_LABEL_Y, PANEL_W, 8, COL_BG);
  _gfx->setTextColor(COL_DIM, COL_BG);
  _gfx->setTextSize(1);
  _gfx->setCursor(8, CAD_LABEL_Y);
  _gfx->print("CADENCE");

  // "RPM" right-justified at size 1
  _gfx->setCursor(PANEL_W - 6 - 18, CAD_LABEL_Y);
  _gfx->print("RPM");

  // Cadence target band: a dim rule with a brighter middle (target zone 80–100 rpm)
  drawCadenceTargetBand();

  // Footer (IP)
  drawStatusBar();
}

// Left-aligned big number with an optional small unit baseline-aligned beside.
// Clears the entire fixed-width box first so changing widths don't leave
// trailing pixels. Matches the Cinder ride layout where "238" sits at the
// left and a small "w" follows.
void St7789Ui::drawNumber(int x, int y, int w, int h, uint8_t textSize, const char* text) {
  _gfx->fillRect(x, y, w, h, COL_BG);
  _gfx->setCursor(x, y);
  _gfx->setTextSize(textSize);
  _gfx->print(text);
}

// Helper: print the number left, then the unit right after it. Keeps the
// number clear-and-redraw inside a fixed-width box so the unit position is
// always relative to the *number*, not the previous frame.
static inline int textPxWidth(const char* s, uint8_t size) {
  return (int)strlen(s) * 6 * size;
}

void St7789Ui::pushPowerHistory(float p) {
  _powerHist[_powerHistHead] = p < 0 ? 0.0f : p;
  _powerHistHead = (_powerHistHead + 1) % HIST_N;
  if (_powerHistCount < HIST_N) _powerHistCount++;
}

// Mini tick-bar mosaic of recent power. Each bar = 1 sample, newest on the
// right. The trailing 4 bars are amber (current trend), the rest are dim —
// matches the Cinder ride visual.
void St7789Ui::drawTickBar() {
  if (!_ready) return;
  // Geometry: 24 slots, 7 px each (5 px bar + 2 px gap) → 168 px. Centred at x=2.
  const int slot_w = 7;
  const int bar_w = 5;
  const int x0 = (PANEL_W - HIST_N * slot_w) / 2; // centred
  // Clear the band
  _gfx->fillRect(0, TICK_Y, PANEL_W, TICK_H, COL_BG);

  // Find the max in the buffer for normalisation, with a floor so a flat low
  // signal doesn't max out the bars.
  float maxv = 50.0f;
  for (uint8_t i = 0; i < _powerHistCount; i++) {
    if (_powerHist[i] > maxv) maxv = _powerHist[i];
  }

  for (uint8_t i = 0; i < HIST_N; i++) {
    // Read in chronological order, oldest at left.
    int idx = (_powerHistHead + HIST_N - _powerHistCount + i) % HIST_N;
    float v = (i < (HIST_N - _powerHistCount)) ? 0.0f : _powerHist[idx];
    int h = (int)((v / maxv) * (TICK_H - 1));
    if (h < 1 && v > 0) h = 1;
    int bx = x0 + i * slot_w;
    int by = TICK_Y + (TICK_H - h);
    // Trailing 4 bars use accent; rest dim.
    uint16_t col = (i >= HIST_N - 4) ? COL_ACCENT : COL_DIM;
    if (h > 0) _gfx->fillRect(bx, by, bar_w, h, col);
  }
}

void St7789Ui::drawAvgMaxLine() {
  if (!_ready) return;
  if (_powerSamples == 0) return;
  int avg = (int)lroundf(_powerSum / (float)_powerSamples);
  int mx  = (int)lroundf(_powerMax);
  if (avg == _lastAvgInt && mx == _lastMaxInt) return;
  _lastAvgInt = avg;
  _lastMaxInt = mx;
  // Clear and repaint a single 8-px row.
  _gfx->fillRect(0, AVGMAX_Y, PANEL_W, 8, COL_BG);
  char buf[32];
  snprintf(buf, sizeof(buf), "AVG %d", avg);
  _gfx->setTextColor(COL_DIM, COL_BG);
  _gfx->setTextSize(1);
  _gfx->setCursor(8, AVGMAX_Y);
  _gfx->print(buf);
  snprintf(buf, sizeof(buf), "MAX %d", mx);
  // Right-aligned: 7 chars * 6 ≈ 42 px wide upper bound; pad fixed.
  int textW = (int)strlen(buf) * 6;
  int x = PANEL_W - textW - 8;
  _gfx->setCursor(x, AVGMAX_Y);
  _gfx->print(buf);
}

// Cadence target zone: rule with a dim band marking 80–100 rpm.
// (The live marker is drawn per-update inside showPower() using the new rpm.)
void St7789Ui::drawCadenceTargetBand() {
  if (!_ready) return;
  // Bar geometry: x=8..164, 4 px tall.
  const int x0 = 8;
  const int x1 = PANEL_W - 8;
  const int bw = x1 - x0;
  // Background rule
  _gfx->fillRect(x0, CAD_BAND_Y, bw, CAD_BAND_H, COL_RULE);
  // Target zone 80–100 rpm shown over 0–160 scale → 50%..62.5%
  int zx0 = x0 + (bw * 80) / 160;
  int zx1 = x0 + (bw * 100) / 160;
  _gfx->fillRect(zx0, CAD_BAND_Y, zx1 - zx0, CAD_BAND_H, COL_DIM);
}

void St7789Ui::showPower(const PowerSample& s, const WorkoutDisplay* /*workout*/) {
  if (!_ready) return;

  // Re-assert backlight duty every tick.
  ledcWrite(PIN_BL, BL_DUTY);

  uint32_t now = millis();
  // Periodic chrome refresh every 30 s — paints over the same pixels (no
  // fillScreen!) so it's silent. The original 5 s + fillScreen flow caused
  // the visible flicker the user reported.
  bool chromeRefresh = (now - _lastFullRedrawMs) > 30000;
  if (chromeRefresh) {
    _lastFullRedrawMs = now;
    drawStaticChrome();
  }

  // Track stats
  if (s.power_w > 0) {
    _powerSum += s.power_w;
    _powerSamples++;
    if (s.power_w > _powerMax) _powerMax = s.power_w;
  }
  pushPowerHistory(s.power_w);

  const int box_x = 8;
  const int box_w = PANEL_W - 16;

  // POWER number — left-aligned, with small "w" unit baseline-aligned to its
  // right (matches the Cinder ride spec).
  if (fabsf(s.power_w - _lastPower) >= 0.5f || chromeRefresh) {
    _lastPower = s.power_w;
    int p = (int)lroundf(s.power_w);
    if (p < 0) p = 0;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", p);
    // Clear box & paint left
    _gfx->fillRect(box_x, POW_NUM_Y, box_w, POW_NUM_H, COL_BG);
    _gfx->setTextColor(COL_INK, COL_BG);
    _gfx->setCursor(box_x, POW_NUM_Y);
    _gfx->setTextSize(7);
    _gfx->print(buf);
    // "w" unit: size 2, baseline ≈ POW_NUM_Y + (size-2)*8 from top
    int numW = textPxWidth(buf, 7);
    int unitX = box_x + numW + 4;
    int unitY = POW_NUM_Y + POW_NUM_H - 16; // baseline of the big number
    _gfx->setTextColor(COL_DIM, COL_BG);
    _gfx->setTextSize(2);
    _gfx->setCursor(unitX, unitY);
    _gfx->print("w");

    // 3s sub-line on the right of the POWER label row — only the value itself
    // is amber (matches "3s · 247" with 247 in accent in the design).
    char sub[16];
    snprintf(sub, sizeof(sub), "%d", p);
    int subValW = textPxWidth(sub, 1);
    int subX = PANEL_W - subValW - 8;
    _gfx->fillRect(80, POW_LABEL_Y, PANEL_W - 80 - 4, 8, COL_BG);
    _gfx->setTextSize(1);
    _gfx->setTextColor(COL_DIM, COL_BG);
    _gfx->setCursor(subX - 24, POW_LABEL_Y);
    _gfx->print("3s ");
    _gfx->setTextColor(COL_ACCENT, COL_BG);
    _gfx->setCursor(subX, POW_LABEL_Y);
    _gfx->print(sub);
  }

  // CADENCE number — same left-aligned style. No unit text (the "RPM" label
  // sits in the top-right of the section header).
  if (fabsf(s.rpm - _lastRpm) >= 0.5f || chromeRefresh) {
    _lastRpm = s.rpm;
    int r = (int)lroundf(s.rpm);
    if (r < 0) r = 0;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", r);
    _gfx->fillRect(box_x, CAD_NUM_Y, box_w, CAD_NUM_H, COL_BG);
    _gfx->setTextColor(COL_INK, COL_BG);
    _gfx->setCursor(box_x, CAD_NUM_Y);
    _gfx->setTextSize(7);
    _gfx->print(buf);

    // Cadence target marker — clear the band, redraw it, then the marker.
    drawCadenceTargetBand();
    if (r > 0 && r < 160) {
      const int x0 = 8;
      const int bw = PANEL_W - 16;
      int mx = x0 + (bw * r) / 160;
      _gfx->fillRect(mx - 1, CAD_BAND_Y - 2, 2, CAD_BAND_H + 4, COL_ACCENT);
    }
  }

  // Tick bar + AVG/MAX update on every tick.
  drawTickBar();
  drawAvgMaxLine();
}

void St7789Ui::showMessage(const char* line1, const char* line2) {
  if (!_ready) return;
  _gfx->fillScreen(COL_BG);
  _gfx->setTextColor(COL_INK, COL_BG);
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
  _lastAvgInt = _lastMaxInt = -2;
}

void St7789Ui::setStatus(const char* text) {
  if (text == nullptr) text = "";
  strncpy(_status, text, sizeof(_status) - 1);
  _status[sizeof(_status) - 1] = '\0';
  drawStatusBar();
}

void St7789Ui::clearAndShowRide() {
  if (!_ready) return;
  _gfx->fillScreen(COL_BG);
  // Force the value-cache to redraw on next showPower().
  _lastPower = -9999.0f;
  _lastRpm = -1.0f;
  _lastAvgInt = _lastMaxInt = -2;
  _lastFullRedrawMs = 0;
  drawStaticChrome();
}

// ─── Cinder boot splash ──────────────────────────────────────────────────────
// Mirrors the Boot variation from the design: top tick rule, amber rotor
// circle with "kp" inside, MONARK / ESP32·CYCLE COMPUTER lockup, BOOT progress
// bar in amber, init-line console below, version footer.
void St7789Ui::showBoot(uint8_t progress, const char* line1,
                        const char* line2, const char* line3) {
  if (!_ready) return;
  _gfx->setTextWrap(false);
  _gfx->fillScreen(COL_BG);

  // Top tick rule
  for (int i = 0; i < 30; i++) {
    int x = 6 + (i * (PANEL_W - 12)) / 29;
    int h = (i % 5 == 0) ? 6 : 3;
    uint16_t c = (i % 5 == 0) ? COL_DIM : COL_RULE;
    _gfx->drawFastVLine(x, 14 - h, h, c);
  }
  _gfx->drawFastHLine(0, 14, PANEL_W, COL_RULE);

  // Rotor logo: amber outer ring + dim inner ring + tick at top + "kp"
  const int cx = PANEL_W / 2;
  const int cy = 96;
  const int r1 = 32;
  const int r2 = 24;
  _gfx->drawCircle(cx, cy, r1, COL_ACCENT);
  _gfx->drawCircle(cx, cy, r1 - 1, COL_ACCENT); // 2-px stroke
  _gfx->drawCircle(cx, cy, r2, COL_DIM);
  _gfx->fillRect(cx, cy - r1 - 2, 1, 6, COL_ACCENT);
  // "kp" centered (size 3 ≈ 18×24 → fits inside r2)
  _gfx->setTextColor(COL_INK, COL_BG);
  _gfx->setTextSize(3);
  _gfx->setCursor(cx - 18, cy - 11);
  _gfx->print("kp");

  // MONARK wordmark (size 2, letter-spaced via spaces — closest we can get
  // without a real font). Centred.
  const char* word = "MONARK";
  int wW = (int)strlen(word) * 6 * 2;
  _gfx->setTextColor(COL_INK, COL_BG);
  _gfx->setTextSize(2);
  _gfx->setCursor((PANEL_W - wW) / 2, 150);
  _gfx->print(word);

  // ESP32 · CYCLE COMPUTER tagline
  const char* tag = "ESP32 CYCLE COMPUTER";
  int tW = (int)strlen(tag) * 6;
  _gfx->setTextColor(COL_DIM, COL_BG);
  _gfx->setTextSize(1);
  _gfx->setCursor((PANEL_W - tW) / 2, 174);
  _gfx->print(tag);

  // BOOT label + progress %
  _gfx->setTextColor(COL_DIM, COL_BG);
  _gfx->setTextSize(1);
  _gfx->setCursor(8, 200);
  _gfx->print("BOOT");
  char pct[8]; snprintf(pct, sizeof(pct), "%u%%", progress);
  int pctW = (int)strlen(pct) * 6;
  _gfx->setTextColor(COL_ACCENT, COL_BG);
  _gfx->setCursor(PANEL_W - 8 - pctW, 200);
  _gfx->print(pct);

  // Progress bar
  _gfx->fillRect(8, 212, PANEL_W - 16, 4, COL_RULE);
  int fillW = ((PANEL_W - 16) * progress) / 100;
  if (fillW > 0) _gfx->fillRect(8, 212, fillW, 4, COL_ACCENT);

  // Init lines
  int y = 228;
  _gfx->setTextColor(COL_DIM, COL_BG);
  _gfx->setTextSize(1);
  if (line1) { _gfx->setCursor(8, y); _gfx->print(line1); y += 12; }
  if (line2) { _gfx->setCursor(8, y); _gfx->print(line2); y += 12; }
  if (line3) { _gfx->setCursor(8, y); _gfx->print(line3); y += 12; }

  // Footer: version left, fw status right
  _gfx->drawFastHLine(0, PANEL_H - 22, PANEL_W, COL_RULE);
  _gfx->setTextColor(COL_DIM, COL_BG);
  _gfx->setTextSize(1);
  _gfx->setCursor(8, PANEL_H - 14);
  _gfx->print("v0.4");
  const char* rh = "fw  ready";
  int rhW = (int)strlen(rh) * 6;
  _gfx->setCursor(PANEL_W - rhW - 8, PANEL_H - 14);
  _gfx->print(rh);
}
