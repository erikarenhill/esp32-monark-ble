#include "TftUi.h"

// Pin definitions: Standard ESP32 VSPI (Safe for Boot)
#define TFT_CS   5
#define TFT_DC   4
#define TFT_MOSI 23
#define TFT_CLK  18
#define TFT_RST  -1
#define TFT_MISO -1

// Small button dimensions
#define SMALL_BTN_W 45
#define SMALL_BTN_H 25

// Modern muted color palette
#define COL_BG        0x1082  // Dark gray background
#define COL_TEXT      0xDEFB  // Off-white text
#define COL_TEXT_DIM  0x9CF3  // Dimmed text
#define COL_POWER     0x2D7F  // Muted teal
#define COL_RPM       0x2D43  // Muted green
#define COL_KP        0xBD40  // Muted gold
#define COL_ACCENT    0x3B7F  // Muted blue accent
#define COL_BTN_START 0x2C83  // Muted green button
#define COL_BTN_PAUSE 0xB3A0  // Muted orange button
#define COL_BTN_STOP  0x9000  // Muted red button
#define COL_BTN_LAP   0x2B5F  // Muted blue button
#define COL_BTN_MENU  0x2945  // Dark button
#define COL_BORDER    0x4A69  // Subtle border

TftUi::TftUi() {
    tft = new Adafruit_ILI9341(TFT_CS, TFT_DC);
    ts = new NS2009(0x48, false, true);

    // Button positions - 3 buttons in a row at bottom
    int bottomY = SCREEN_H - BTN_H - BTN_MARGIN;
    int btnSpacing = (SCREEN_W - 3 * BTN_W) / 4;  // Equal spacing for 3 buttons

    // When no workout: START centered
    _startBtn = {(SCREEN_W - BTN_W) / 2, bottomY, BTN_W, BTN_H};

    // When workout active: 3 buttons in a row (PAUSE/RESUME, LAP, STOP)
    _pauseBtn = {btnSpacing, bottomY, BTN_W, BTN_H};
    _lapBtn = {btnSpacing * 2 + BTN_W, bottomY, BTN_W, BTN_H};
    _stopBtn = {btnSpacing * 3 + BTN_W * 2, bottomY, BTN_W, BTN_H};

    // Small menu button (top right corner)
    _smallMenuBtn = {SCREEN_W - SMALL_BTN_W - 4, 4, SMALL_BTN_W, SMALL_BTN_H};

    // Setup menu items
    _menu.setTitle("MENU");
    _menu.addItem("Calibrate", MenuAction::Calibration);
    _menu.addItem("View Cal", MenuAction::ViewCalibration);
    _menu.addItem("Close", MenuAction::Close);

    // Setup power chart (60 second window)
    _powerChart.setTimeWindow(60000);
    _powerChart.setPosition(5, 145, 230, 65);
    _powerChart.setColors(0x2D7F, 0x2945, 0x1082);  // Muted teal line, dark grid, dark bg
    _powerChart.setPadding(0.10f);
    _powerChart.setGridLines(6, 3);
}

void TftUi::begin() {
    pinMode(TFT_DC, OUTPUT);
    tft->begin();
    tft->setRotation(0);
    tft->fillScreen(COL_BG);

    Wire.begin();
    Wire.setClock(400000);  // 400kHz Fast Mode for responsive touch

    Serial.println("Scanning I2C bus...");
    byte count = 0;
    for (byte i = 1; i < 127; i++) {
        Wire.beginTransmission(i);
        if (Wire.endTransmission() == 0) {
            Serial.print("I2C device found at 0x");
            if (i < 16) Serial.print("0");
            Serial.println(i, HEX);
            count++;
        }
    }
    if (count == 0) Serial.println("No I2C devices found");
    Serial.println("NS2009 initialized");
}

void TftUi::drawButton(int x, int y, int w, int h, const char* label, uint16_t color, bool selected) {
    tft->fillRect(x, y, w, h, color);
    if (selected) {
        tft->drawRect(x, y, w, h, COL_ACCENT);
        tft->drawRect(x+1, y+1, w-2, h-2, COL_ACCENT);
    } else {
        tft->drawRect(x, y, w, h, COL_BORDER);
    }

    tft->setTextSize(1);  // Smaller font for workout buttons
    tft->setTextColor(COL_TEXT);

    int16_t x1, y1;
    uint16_t tw, th;
    tft->getTextBounds(label, 0, 0, &x1, &y1, &tw, &th);
    tft->setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    tft->print(label);
}

void TftUi::drawSmallButton(int x, int y, int w, int h, const char* label, uint16_t color) {
    tft->fillRect(x, y, w, h, color);
    tft->drawRect(x, y, w, h, COL_BORDER);

    tft->setTextSize(1);
    tft->setTextColor(COL_TEXT_DIM);

    int16_t x1, y1;
    uint16_t tw, th;
    tft->getTextBounds(label, 0, 0, &x1, &y1, &tw, &th);
    tft->setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    tft->print(label);
}

void TftUi::drawWorkoutButtons(const WorkoutDisplay* workout) {
    bool active = workout && workout->active;
    bool running = workout && workout->running;
    bool paused = workout && workout->paused;

    // Clear bottom button row
    int clearY = SCREEN_H - BTN_H - BTN_MARGIN * 2;
    tft->fillRect(0, clearY, SCREEN_W, SCREEN_H - clearY, COL_BG);

    if (!active) {
        // Show START button centered
        drawButton(_startBtn.x, _startBtn.y, _startBtn.w, _startBtn.h, "START", COL_BTN_START);
    } else {
        // Workout active - 3 buttons in a row
        if (running) {
            drawButton(_pauseBtn.x, _pauseBtn.y, _pauseBtn.w, _pauseBtn.h, "PAUSE", COL_BTN_PAUSE);
        } else if (paused) {
            drawButton(_pauseBtn.x, _pauseBtn.y, _pauseBtn.w, _pauseBtn.h, "RESUME", COL_BTN_START);
        }
        drawButton(_lapBtn.x, _lapBtn.y, _lapBtn.w, _lapBtn.h, "LAP", COL_BTN_LAP);
        drawButton(_stopBtn.x, _stopBtn.y, _stopBtn.w, _stopBtn.h, "STOP", COL_BTN_STOP);
    }

    // Draw small menu button at top right
    drawSmallButton(_smallMenuBtn.x, _smallMenuBtn.y, _smallMenuBtn.w, _smallMenuBtn.h, "MENU", COL_BTN_MENU);
}

bool TftUi::isTouchInRect(int tx, int ty, const Rect& r) {
    return tx >= r.x && tx <= r.x + r.w && ty >= r.y && ty <= r.y + r.h;
}

void TftUi::renderMenu() {
    if (_menuDrawn) return;

    tft->fillScreen(COL_BG);

    tft->setTextSize(2);
    tft->setTextColor(COL_TEXT);
    int16_t x1, y1;
    uint16_t tw, th;
    tft->getTextBounds(_menu.getTitle(), 0, 0, &x1, &y1, &tw, &th);
    tft->setCursor((SCREEN_W - tw) / 2, 30);
    tft->print(_menu.getTitle());

    int btnW = SCREEN_W - 16;  // Full width menu buttons
    for (int i = 0; i < _menu.getItemCount(); i++) {
        const MenuItem* item = _menu.getItem(i);
        if (item) {
            int y = MENU_START_Y + i * (MENU_BTN_H + 10);
            bool selected = (i == _menu.getSelectedIndex());
            uint16_t color = (i == 0) ? COL_BTN_LAP : COL_BTN_STOP;
            drawButton(8, y, btnW, MENU_BTN_H, item->label, color, selected);
        }
    }

    _menuDrawn = true;
}

int TftUi::getTouchedMenuIndex(int tx, int ty) {
    int btnW = SCREEN_W - 16;
    for (int i = 0; i < _menu.getItemCount(); i++) {
        int y = MENU_START_Y + i * (MENU_BTN_H + 10);
        if (tx >= 8 && tx <= 8 + btnW &&
            ty >= y && ty <= y + MENU_BTN_H) {
            return i;
        }
    }
    return -1;
}

void TftUi::update() {
    uint32_t now = millis();

    // Track menu state - detect when menu closes
    if (_lastMenuOpen && !_menu.isOpen()) {
        _needsFullRedraw = true;
    }
    _lastMenuOpen = _menu.isOpen();

    if (_menu.isOpen()) {
        renderMenu();
    }

    // Always scan touch - this reads Z (pressure), X and Y in one go
    ts->Scan();
    bool touchNow = ts->Touched;

    // Edge detection: only trigger on touch START (rising edge)
    bool touchStart = touchNow && !_touchPrev;
    _touchPrev = touchNow;

    if (!touchStart) {
        return;
    }

    // Debounce
    if (now - _lastTriggerTime < DEBOUNCE_MS) {
        return;
    }

    // Get coordinates (already read by Scan())
    int tx = ts->X;
    int ty = ts->Y;

    // Validate
    if (tx < 0 || tx > SCREEN_W || ty < 0 || ty > SCREEN_H) {
        return;
    }

    _lastTriggerTime = now;
    Serial.printf("Touch: %d,%d\n", tx, ty);

    if (_menu.isOpen()) {
        int index = getTouchedMenuIndex(tx, ty);
        Serial.printf("Menu touch at %d,%d -> item %d\n", tx, ty, index);
        if (index >= 0) {
            _menu.selectByIndex(index);
            _menuDrawn = false;
            tft->fillScreen(COL_BG);
            _chartInitialized = false;  // Redraw chart area
        }
        return;  // Don't process other buttons when menu is open
    }
    else if (_inMessageMode) {
        // NEXT button - signal action and exit message mode
        Rect nextBtn = {150, 270, 80, 40};
        if (isTouchInRect(tx, ty, nextBtn)) {
            Serial.println("Next pressed");
            _menuRequested = true;   // Signal to CalibrationProcess
            _inMessageMode = false;  // Exit message mode
            _needsFullRedraw = true; // Trigger redraw
        }
        return;
    }
    else {
        // Small menu button (always check first)
        if (isTouchInRect(tx, ty, _smallMenuBtn)) {
            Serial.println("Menu pressed");
            _menu.open();
            _menuDrawn = false;
            return;
        }

        // Check buttons based on workout state
        Serial.printf("_lastWorkoutActive = %d\n", _lastWorkoutActive);
        if (!_lastWorkoutActive) {
            // No workout - only START button
            if (isTouchInRect(tx, ty, _startBtn)) {
                Serial.println("Start pressed");
                _workoutAction = MenuAction::Start;
                return;  // Exit after handling
            }
        } else {
            // Workout active - PAUSE/LAP/STOP buttons
            if (isTouchInRect(tx, ty, _pauseBtn)) {
                Serial.println("Pause/Resume pressed");
                _workoutAction = MenuAction::Pause;
                return;
            }
            if (isTouchInRect(tx, ty, _lapBtn)) {
                Serial.println("Lap pressed");
                _workoutAction = MenuAction::Lap;
                return;
            }
            if (isTouchInRect(tx, ty, _stopBtn)) {
                Serial.println("Stop pressed");
                _workoutAction = MenuAction::Stop;
                return;
            }
        }
    }
}

bool TftUi::isActionRequested() {
    if (_menuRequested) {
        _menuRequested = false;
        return true;
    }
    return false;
}

MenuAction TftUi::getWorkoutAction() {
    MenuAction action = _workoutAction;
    _workoutAction = MenuAction::None;
    return action;
}

void TftUi::showPower(const PowerSample& s, const WorkoutDisplay* workout) {
    static bool _firstDraw = true;

    // Don't draw power display while showing a message
    if (_inMessageMode) {
        return;
    }

    // Check if we need a full redraw (after menu close)
    if (_needsFullRedraw) {
        tft->fillScreen(COL_BG);
        _menuDrawn = false;
        _chartInitialized = false;
        _firstDraw = true;
        _needsFullRedraw = false;
    }

    // Check if buttons need redrawing
    bool workoutActive = workout && workout->active;
    bool workoutRunning = workout && workout->running;
    if (_firstDraw || workoutActive != _lastWorkoutActive || workoutRunning != _lastWorkoutRunning) {
        // Clear screen when layout changes (idle <-> workout)
        if (workoutActive != _lastWorkoutActive) {
            tft->fillScreen(COL_BG);
        }
        _lastWorkoutActive = workoutActive;
        _lastWorkoutRunning = workoutRunning;
        drawWorkoutButtons(workout);
        _firstDraw = false;

        // Clear/reset chart when workout state changes
        if (!workoutActive) {
            _powerChart.clear();
        }
        _chartInitialized = false;
    }

    // Add power to chart if workout is active
    if (workoutActive && workout) {
        _powerChart.addPoint(workout->elapsedMs, s.power_w);
    }

    int16_t x1, y1;
    uint16_t tw, th;
    char powerStr[10];
    char rpmStr[10];
    snprintf(powerStr, sizeof(powerStr), "%d", (int)s.power_w);
    snprintf(rpmStr, sizeof(rpmStr), "%d", (int)s.rpm);

    if (!workoutActive) {
        // === IDLE MODE: Large centered display ===

        // === POWER (extra large, centered) ===
        tft->fillRect(0, 50, SCREEN_W, 80, COL_BG);
        tft->setTextSize(7);
        tft->setTextColor(COL_POWER);
        tft->getTextBounds(powerStr, 0, 0, &x1, &y1, &tw, &th);
        tft->setCursor((SCREEN_W - tw) / 2 - 20, 55);
        tft->print(powerStr);

        tft->setTextSize(2);
        tft->setTextColor(COL_TEXT_DIM);
        tft->setCursor((SCREEN_W + tw) / 2 - 15, 80);
        tft->print("W");

        // === RPM (larger, centered below) ===
        tft->fillRect(0, 140, SCREEN_W, 60, COL_BG);
        tft->setTextSize(5);
        tft->setTextColor(COL_RPM);
        tft->getTextBounds(rpmStr, 0, 0, &x1, &y1, &tw, &th);
        tft->setCursor((SCREEN_W - tw) / 2 - 25, 150);
        tft->print(rpmStr);

        tft->setTextSize(2);
        tft->setTextColor(COL_TEXT_DIM);
        tft->setCursor((SCREEN_W + tw) / 2 - 20, 165);
        tft->print("rpm");

        // === KP (centered below RPM) ===
        tft->fillRect(0, 210, SCREEN_W, 25, COL_BG);
        tft->setTextSize(2);
        tft->setTextColor(COL_KP);
        char kpStr[16];
        snprintf(kpStr, sizeof(kpStr), "KP: %.1f", s.kp);
        tft->getTextBounds(kpStr, 0, 0, &x1, &y1, &tw, &th);
        tft->setCursor((SCREEN_W - tw) / 2, 215);
        tft->print(kpStr);
    } else {
        // === WORKOUT MODE: Compact display with chart ===

        // === POWER (medium, top area) ===
        tft->fillRect(0, 35, SCREEN_W, 55, COL_BG);
        tft->setTextSize(5);
        tft->setTextColor(COL_POWER);
        tft->getTextBounds(powerStr, 0, 0, &x1, &y1, &tw, &th);
        tft->setCursor((SCREEN_W - tw) / 2 - 15, 40);
        tft->print(powerStr);

        tft->setTextSize(2);
        tft->setTextColor(COL_TEXT_DIM);
        tft->setCursor((SCREEN_W + tw) / 2 - 10, 55);
        tft->print("W");

        // === RPM (smaller, below power) ===
        tft->fillRect(0, 95, SCREEN_W, 40, COL_BG);
        tft->setTextSize(3);
        tft->setTextColor(COL_RPM);
        tft->getTextBounds(rpmStr, 0, 0, &x1, &y1, &tw, &th);
        tft->setCursor((SCREEN_W - tw) / 2 - 20, 100);
        tft->print(rpmStr);

        tft->setTextSize(1);
        tft->setTextColor(COL_TEXT_DIM);
        tft->setCursor((SCREEN_W + tw) / 2 - 15, 110);
        tft->print("rpm");

        // === KP (small, top left) ===
        tft->fillRect(0, 0, 50, 12, COL_BG);
        tft->setCursor(2, 2);
        tft->setTextSize(1);
        tft->setTextColor(COL_KP);
        tft->print("KP:");
        tft->print(s.kp, 1);
    }

    // === Timer and workout info OR chart ===
    if (workoutActive && workout) {
        // Draw power chart
        _powerChart.draw(tft);

        // Timer above buttons
        char timeStr[12];
        Workout::formatTime(workout->elapsedMs, timeStr, sizeof(timeStr));

        int timerY = 220;
        tft->fillRect(0, timerY, SCREEN_W, 25, COL_BG);
        tft->setTextSize(2);
        tft->setTextColor(workout->paused ? COL_BTN_PAUSE : COL_TEXT);
        tft->getTextBounds(timeStr, 0, 0, &x1, &y1, &tw, &th);
        tft->setCursor((SCREEN_W - tw) / 2, timerY);
        tft->print(timeStr);

        // Avg power and lap info below timer
        int avgY = timerY + 22;
        tft->fillRect(0, avgY, SCREEN_W - 50, 15, COL_BG);
        tft->setTextSize(1);
        tft->setTextColor(COL_KP);
        tft->setCursor(5, avgY + 3);
        tft->print("Avg:");
        tft->print((int)workout->avgPower);
        tft->print("W");

        if (workout->lapNumber > 0) {
            tft->setCursor(80, avgY + 3);
            tft->print("L");
            tft->print(workout->lapNumber);
            tft->print(":");
            tft->print((int)workout->lapAvgPower);
            tft->print("W");
        }
    } else {
        // No workout - clear chart area
        if (!_chartInitialized) {
            tft->fillRect(0, 135, SCREEN_W, 140, COL_BG);
            _chartInitialized = true;
        }
    }
}

void TftUi::showMessage(const char* line1, const char* line2) {
    bool justEntered = false;
    if (!_inMessageMode) {
        tft->fillScreen(COL_BG);
        _inMessageMode = true;
        _menuDrawn = false;
        _chartInitialized = false;
        justEntered = true;
    }

    tft->setTextColor(COL_TEXT, COL_BG);
    tft->setTextSize(2);

    if (line1) {
        tft->setCursor(20, 80);
        tft->print(line1);
        tft->print("        ");
    }
    if (line2) {
        tft->setCursor(20, 120);
        tft->print(line2);
        tft->print("        ");
    }

    if (justEntered) {
        drawButton(150, 270, 80, 40, "NEXT", COL_BTN_LAP);
    }
}
