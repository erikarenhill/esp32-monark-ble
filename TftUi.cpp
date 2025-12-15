#include "TftUi.h"

// Pin definitions: Standard ESP32 VSPI (Safe for Boot)
// We are switching to these because the Olimex pins (12, 15) are "Strapping Pins"
// that can prevent the ESP32 from booting if the display pulls them high/low.
#define TFT_CS   5   // VSPI CS0
#define TFT_DC   4   // Safe GPIO
#define TFT_MOSI 23  // VSPI MOSI
#define TFT_CLK  18  // VSPI SCK
#define TFT_RST  -1  // Connect to 3.3V or EN
#define TFT_MISO -1  // Not used

TftUi::TftUi() {
    // Hardware SPI for best performance (uses VSPI: MOSI=23, SCK=18 automatically)
    tft = new Adafruit_ILI9341(TFT_CS, TFT_DC);
    ts = new NS2009(0x48, false, true); // Address 0x48, flip Y
}

void TftUi::begin() {
    // Pin setup
    pinMode(TFT_DC, OUTPUT);
    
    // Initialize Display
    tft->begin();
    tft->setRotation(0); // Portrait
    tft->fillScreen(ILI9341_BLACK);
    
    // Initialize I2C for Touch
    Wire.begin(); // Uses default SDA=21, SCL=22
    
    // I2C Scanner to debug connection
    Serial.println("Scanning I2C bus...");
    byte count = 0;
    for(byte i = 1; i < 127; i++) {
        Wire.beginTransmission(i);
        if (Wire.endTransmission() == 0) {
            Serial.print("I2C device found at address 0x");
            if (i < 16) Serial.print("0");
            Serial.println(i, HEX);
            count++;
        }
    }
    if (count == 0) Serial.println("No I2C devices found\n");
    else Serial.println("done\n");

    // NS2009 doesn't have a "begin" method that returns success/fail like STMPE610
    // It just works if I2C is up. We can try to read a register to verify.
    // But for now, we assume it's there since scanner found 0x48.
    Serial.println("NS2009 initialized (assumed)");
    
    tft->setTextSize(1);
    tft->setTextColor(ILI9341_WHITE, ILI9341_BLACK);
    tft->setCursor(10, 5);
    tft->print("Monark ESP32");
}

void TftUi::drawButton(const char* label, uint16_t color) {
    // Draw button at bottom right (Portrait 240x320)
    int w = 80;
    int h = 40;
    int x = 240 - w - 10;
    int y = 320 - h - 10;
    
    tft->fillRect(x, y, w, h, color);
    tft->drawRect(x, y, w, h, ILI9341_WHITE);
    
    // Center text
    tft->setTextSize(2);
    tft->setTextColor(ILI9341_WHITE);
    
    int16_t x1, y1;
    uint16_t tw, th;
    tft->getTextBounds(label, 0, 0, &x1, &y1, &tw, &th);
    
    tft->setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    tft->print(label);
}

bool TftUi::isTouchInButton() {
    // NS2009 Logic
    if (ts->CheckTouched()) {
        ts->Scan(); // Updates ts->X and ts->Y

        int x = ts->X;
        int y = ts->Y;

        // Debug touch
        Serial.print("Touch: "); Serial.print(x); Serial.print(", "); Serial.println(y);

        // Check bounds of bottom right button
        int w = 80;
        int h = 40;
        int bx = 240 - w - 10;
        int by = 320 - h - 10;

        if (x >= bx && x <= (bx + w) && y >= by && y <= (by + h)) {
            Serial.println("Button hit!");
            return true;
        }
    }
    return false;
}

void TftUi::update() {
    bool touched = isTouchInButton();

    if (touched && !_wasTouched) {
        uint32_t now = millis();
        if (now - _lastActionTime > DEBOUNCE_MS) {
            _actionPressed = true;
            _lastActionTime = now;
        }
    }
    _wasTouched = touched;
}

bool TftUi::isActionRequested() {
    if (_actionPressed) {
        _actionPressed = false;
        return true;
    }
    return false;
}

void TftUi::showPower(const PowerSample& s, float cycleConstant) {
    if (_inMessageMode) {
        tft->fillScreen(ILI9341_BLACK);
        tft->setCursor(10, 5);
        tft->setTextSize(2);
        tft->print("Monark ESP32");
        _inMessageMode = false;
    }

    // Layout (Portrait 240x320):
    // Power (Watts) - Big Center
    // RPM - Bottom Left
    // KP - Top Right
    
    // Power
    // Clear previous value area
    tft->fillRect(40, 100, 160, 60, ILI9341_BLACK);
    
    tft->setCursor(60, 110);
    tft->setTextSize(4);
    tft->setTextColor(ILI9341_CYAN);
    tft->print((int)s.power_w);
    tft->setTextSize(2);
    tft->print(" W");
    
    // RPM
    tft->fillRect(10, 220, 100, 30, ILI9341_BLACK);
    tft->setCursor(10, 220);
    tft->setTextSize(2);
    tft->setTextColor(ILI9341_GREEN);
    tft->print("RPM: ");
    tft->print((int)s.rpm);
    
    // KP
    tft->fillRect(130, 5, 110, 30, ILI9341_BLACK);
    tft->setCursor(130, 5);
    tft->setTextSize(2);
    tft->setTextColor(ILI9341_YELLOW);
    tft->print("KP: ");
    tft->print(s.kp, 2);
    
    // ADC (Debug)
    tft->fillRect(10, 30, 100, 20, ILI9341_BLACK);
    tft->setCursor(10, 30);
    tft->setTextSize(1);
    tft->setTextColor(ILI9341_DARKGREY);
    tft->print("ADC: ");
    tft->print(s.adc_raw);

    // Button
    drawButton("MENU", ILI9341_BLUE);
}

void TftUi::showMessage(const char* line1, const char* line2) {
    bool justEntered = false;
    if (!_inMessageMode) {
        tft->fillScreen(ILI9341_BLACK);
        _inMessageMode = true;
        justEntered = true;
    }

    tft->setTextColor(ILI9341_WHITE, ILI9341_BLACK); // Background color for overwrite
    tft->setTextSize(2);

    if (line1) {
        tft->setCursor(20, 80);
        tft->print(line1);
        tft->print("        "); // Clear trailing chars
    }
    if (line2) {
        tft->setCursor(20, 120);
        tft->print(line2);
        tft->print("        "); // Clear trailing chars
    }

    // Only draw button on first entry to reduce flicker
    if (justEntered) {
        drawButton("NEXT", ILI9341_RED);
    }
}
