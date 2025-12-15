#pragma once

// Default Configuration
#ifndef LCD_ADDR
#define LCD_ADDR 0x27
#endif

#if defined(BOARD_ESP32DEV)
    // ESP32 Dev Module (WROOM-32)
    static const int I2C_SDA_PIN = 21;
    static const int I2C_SCL_PIN = 22;
    static const int CADENCE_PIN = 27;
    static const int ADC_PIN     = 34;
    static const int CAL_BUTTON_PIN = 0; // BOOT button

#elif defined(BOARD_WEMOS_D1_MINI32)
    // Wemos D1 Mini ESP32
    // SDA=21, SCL=22 are standard but on D1 mini form factor they might be mapped differently on the silk screen
    // D1 Mini 32: SDA=GPIO21, SCL=GPIO22
    static const int I2C_SDA_PIN = 21;
    static const int I2C_SCL_PIN = 22;
    static const int CADENCE_PIN = 17; // Example: D4 on Wemos might be different
    static const int ADC_PIN     = 34;
    static const int CAL_BUTTON_PIN = 0; // BOOT button

#else
    // Fallback / Generic ESP32
    static const int I2C_SDA_PIN = 21;
    static const int I2C_SCL_PIN = 22;
    static const int CADENCE_PIN = 27; // Default to 27 as requested
    static const int ADC_PIN     = 34;
    static const int CAL_BUTTON_PIN = 0; // BOOT button
#endif
