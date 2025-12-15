#pragma once
#include "IDisplay.h"
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
    void showPower(const PowerSample& s, float cycleConstant) override;
    void showMessage(const char* line1, const char* line2) override;
    
    void update() override;
    bool isActionRequested() override;

private:
    Adafruit_ILI9341* tft;
    NS2009* ts;

    // Button state
    bool _actionPressed = false;
    bool _wasTouched = false;
    uint32_t _lastActionTime = 0;
    static const uint32_t DEBOUNCE_MS = 300;

    // UI State
    bool _inMessageMode = false;

    // Helper to draw a button
    void drawButton(const char* label, uint16_t color);
    bool isTouchInButton();
};
