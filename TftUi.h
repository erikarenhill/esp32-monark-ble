#pragma once
#include "IDisplay.h"
#include "Menu.h"
#include "Workout.h"
#include "LineChart.h"
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "NS2009.h"

class TftUi : public IDisplay {
public:
    TftUi();

    void begin() override;
    void showPower(const PowerSample& s, const WorkoutDisplay* workout = nullptr) override;
    void showMessage(const char* line1, const char* line2) override;

    void update() override;
    bool isActionRequested() override;

    // Menu access (override from IDisplay)
    Menu* getMenu() override { return &_menu; }
    bool isMenuOpen() const override { return _menu.isOpen(); }

    // Workout actions
    MenuAction getWorkoutAction() override;

private:
    Adafruit_ILI9341* tft;
    NS2009* ts;
    Menu _menu;

    // Touch state
    bool _touchPrev = false;          // Previous frame touch state
    uint32_t _lastTriggerTime = 0;    // When last button was triggered
    static const uint32_t DEBOUNCE_MS = 100;

    // UI State
    bool _inMessageMode = false;
    bool _menuRequested = false;
    bool _menuDrawn = false;
    MenuAction _workoutAction = MenuAction::None;

    // Layout constants
    static const int SCREEN_W = 240;
    static const int SCREEN_H = 320;
    static const int BTN_H = 32;       // Workout buttons (medium)
    static const int BTN_W = 58;
    static const int BTN_MARGIN = 6;
    static const int MENU_START_Y = 70;
    static const int MENU_BTN_H = 60;  // Menu buttons (larger)

    // Button positions
    struct Rect { int x, y, w, h; };
    Rect _startBtn;
    Rect _pauseBtn;
    Rect _stopBtn;
    Rect _lapBtn;
    Rect _smallMenuBtn;  // Small menu button (always visible)

    // Power chart
    LineChart _powerChart;
    bool _chartInitialized = false;

    // Track last workout state for button changes
    bool _lastWorkoutActive = false;
    bool _lastWorkoutRunning = false;
    bool _lastMenuOpen = false;
    bool _needsFullRedraw = false;  // Set when menu closes to trigger redraw

    // Helpers
    void drawButton(int x, int y, int w, int h, const char* label, uint16_t color, bool selected = false);
    void drawSmallButton(int x, int y, int w, int h, const char* label, uint16_t color);
    void drawWorkoutButtons(const WorkoutDisplay* workout);
    void renderMenu();
    int getTouchedMenuIndex(int tx, int ty);
    bool isTouchInRect(int tx, int ty, const Rect& r);
};
