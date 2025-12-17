# wroom-monark

A Cycling Power Service (CPS) simulator and sensor interface for Monark exercise bikes, running on ESP32.

## Overview

This project turns an ESP32 into a Bluetooth Low Energy (BLE) Cycling Power Meter. It currently simulates power data (RPM, Watts) but is designed to interface with real sensors on a Monark ergometer. It broadcasts standard CPS data, making it compatible with cycling apps like Zwift, TrainerRoad, and others.

## Features

- **BLE Cycling Power Service:** Implements the standard GATT service (UUID `0x1818`) to broadcast power and crank revolution data.
- **Simulation Mode:** Generates realistic random cycling data (RPM: 85-95, Power: 150-200W) for testing and development.
- **LCD Display:** Supports a 1602 I2C LCD to show real-time metrics:
  - RPM (Cadence)
  - KP (Kilopond resistance)
  - Power (Watts)
- **Modular Design:** Uses a `PowerSource` interface, allowing easy switching between the simulator and a real hardware sensor implementation.

## Hardware Requirements

- **Microcontroller:** ESP32 (e.g., ESP32-WROOM-32)
- **Display Options:**
  - **Option A: LCD 1602 (I2C)**
    - SDA -> GPIO 21
    - SCL -> GPIO 22
  - **Option B: ILI9341 TFT with Touch**
    - See [tft-wroom.md](tft-wroom.md) for complete wiring guide

- **Sensors:**
  - Cadence Sensor -> GPIO 27 (Note: Avoid GPIO 17 if using TFT)
  - ADC Input (Force) -> GPIO 34
  - Calibration Button -> GPIO 0 (Boot Button)

## Dependencies

This project relies on the following Arduino libraries:
- **NimBLE-Arduino**: For efficient Bluetooth Low Energy communication.
- **LiquidCrystal_I2C**: For controlling the LCD display.

## Configuration

Settings can be adjusted in `wroom-monark.ino`:

```cpp
// Toggle between simulation and real sensor (if implemented)
static const bool USE_SIMULATOR = true;

// Monark cycle constant (depends on the specific bike model)
static const float CYCLE_CONSTANT = 1.05f;

// I2C Pins for LCD
static const int I2C_SDA = 21;
static const int I2C_SCL = 22;
static const uint8_t LCD_ADDR = 0x27;
```

## File Structure

- `wroom-monark.ino`: Main entry point. Handles setup, the main loop, and coordinates components.
- `BleCps.h/cpp`: Handles BLE advertising and notifications using the Cycling Power Service.
- `PowerSimulator.h/cpp`: Generates fake cycling data for testing.
- `LcdUi1602.h/cpp`: Manages the I2C LCD display.
- `PowerSource.h`: Abstract base class for power data sources.
- `PowerSample.h`: Data structure for passing cycling metrics.

## Usage

1. Install the required libraries in your Arduino IDE or PlatformIO.
2. Flash the code to your ESP32.
3. The device will start advertising as **"LT2-PowerSim"**.
4. Connect to it using a BLE scanner or a cycling app.
5. The LCD will display the simulated or measured values.
