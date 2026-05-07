#pragma once
#include "PowerSample.h"
#include "Menu.h"

struct WorkoutDisplay {
    bool active = false;
    bool running = false;
    bool paused = false;
    uint32_t elapsedMs = 0;
    uint32_t lapMs = 0;
    float avgPower = 0;
    float lapAvgPower = 0;
    int lapNumber = 0;
};

class IDisplay {
public:
    virtual ~IDisplay() {}

    // Initialize the display hardware
    virtual void begin() = 0;

    // Show the main power screen
    virtual void showPower(const PowerSample& s, const WorkoutDisplay* workout = nullptr) = 0;

    // Show a 2-line message (used for calibration/status)
    virtual void showMessage(const char* line1, const char* line2) = 0;

    // Small persistent status line (e.g. WiFi IP). Default: noop.
    virtual void setStatus(const char* /*text*/) {}

    // Cinder-style boot splash. Default: noop. Concrete impls render a
    // themed splash with a progress percentage and up to three init lines.
    virtual void showBoot(uint8_t /*progress*/,
                          const char* /*line1*/ = nullptr,
                          const char* /*line2*/ = nullptr,
                          const char* /*line3*/ = nullptr) {}

    // Interaction
    virtual void update() {}
    virtual bool isActionRequested() { return false; }

    // Menu support (optional - displays without menu return defaults)
    virtual Menu* getMenu() { return nullptr; }
    virtual bool isMenuOpen() const { return false; }

    // Workout actions (for displays with workout buttons)
    virtual MenuAction getWorkoutAction() { return MenuAction::None; }
};
