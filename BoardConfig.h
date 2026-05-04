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

#elif defined(BOARD_WAVESHARE_C6_LCD_1_47)
    // Waveshare ESP32-C6-LCD-1.47 (RISC-V, ST7789 172x320)
    // Onboard usage:
    //   LCD (SPI):  MOSI=6, SCLK=7, CS=14, DC=15, RST=21, BL=22
    //   TF card:    MISO=5, MOSI=6, SCLK=7, CS=4   (so GPIO 4, 5 are NOT free)
    //   RGB LED:    GPIO 8
    //   BOOT btn:   GPIO 9 (reused for calibration)
    //   USB D-/D+:  GPIO 12 / 13
    // Headers break out: left = 3V3/GND/5V/0/1/2/3/4/5; right = 23/22/21/20/19/18/13/17(RXD)/16(TXD).
    // Strapping pins to avoid as inputs at boot: 4, 5, 8, 9, 15.
    static const int I2C_SDA_PIN = 19; // free, not strapping
    static const int I2C_SCL_PIN = 20;
    static const int CADENCE_PIN = 18; // right header, digital, internal pull-up, reed/hall to GND
    static const int ADC_PIN     = 1;  // left header, ADC1_CH1, potentiometer wiper
    static const int CAL_BUTTON_PIN = 9; // onboard BOOT button

#else
    // Fallback / Generic ESP32
    static const int I2C_SDA_PIN = 21;
    static const int I2C_SCL_PIN = 22;
    static const int CADENCE_PIN = 27;
    static const int ADC_PIN     = 34;
    static const int CAL_BUTTON_PIN = 0; // BOOT button
#endif
