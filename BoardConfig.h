#pragma once

// Default Configuration
#ifndef LCD_ADDR
#define LCD_ADDR 0x27
#endif

// Set either of these to 255 to disable.
//
// TARE_BUTTON_PIN: optional dedicated button that zeroes the brake reading
//   with a single press. Leave at 255 if the board has no spare button --
//   the calibration button below then does tare on a short press.
// STATUS_LED_PIN: blinks to confirm a tare (2 slow = OK, 5 fast = rejected).
//   GPIO 2 is the onboard LED on most ESP32 dev boards. Set to 255 if the
//   pin is used for something else on your board.

#if defined(BOARD_ESP32DEV)
    // ESP32 Dev Module (WROOM-32)
    static const int I2C_SDA_PIN = 21;
    static const int I2C_SCL_PIN = 22;
    static const int CADENCE_PIN = 27;
    static const int ADC_PIN     = 34;
    static const int CAL_BUTTON_PIN = 0; // BOOT button
    static const int TARE_BUTTON_PIN = 255;
    static const int STATUS_LED_PIN  = 2;

#elif defined(BOARD_WEMOS_D1_MINI32)
    // Wemos D1 Mini ESP32
    // SDA=21, SCL=22 are standard but on D1 mini form factor they might be mapped differently on the silk screen
    // D1 Mini 32: SDA=GPIO21, SCL=GPIO22
    static const int I2C_SDA_PIN = 21;
    static const int I2C_SCL_PIN = 22;
    static const int CADENCE_PIN = 17; // Example: D4 on Wemos might be different
    static const int ADC_PIN     = 34;
    static const int CAL_BUTTON_PIN = 0; // BOOT button
    static const int TARE_BUTTON_PIN = 255;
    static const int STATUS_LED_PIN  = 2;

#else
    // Fallback / Generic ESP32
    static const int I2C_SDA_PIN = 21;
    static const int I2C_SCL_PIN = 22;
    static const int CADENCE_PIN = 27;
    static const int ADC_PIN     = 34;
    static const int CAL_BUTTON_PIN = 0; // BOOT button
    static const int TARE_BUTTON_PIN = 255;
    static const int STATUS_LED_PIN  = 2;
#endif
