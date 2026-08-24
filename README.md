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

## Calibration and Zeroing

Two separate things, and they fix different problems:

- **Full calibration** (4 points: 0 / 6 / 4 / 2 kp) sets the *span* -- how many
  millivolts correspond to one kp. Needed once per install.
- **Zero / tare** sets the *zero point*. The reading drifts a few millivolts
  between sessions, and because the whole 0-6 kp range is only ~150 mV, a few mV
  is worth a few tenths of a kp at every load. That shows up as power that is far
  too high at low resistance and roughly right at high resistance -- and it cannot
  be corrected with the cycle constant, because the error is additive while the
  cycle constant is multiplicative.

Zero the bike before each ride, at a standstill with the brake at its lowest
setting. Taring shifts the calibration curve without touching the span, so it
never invalidates a full calibration. A full calibration clears the tare offset.

### Physical button

The calibration button (`CAL_BUTTON_PIN`, the BOOT button by default) does both:

| Gesture | Action |
|---|---|
| Short press | Zero / tare |
| Hold 3 seconds | Start full 4-point calibration (hold again to cancel) |

Feedback on `STATUS_LED_PIN` (GPIO 2 by default, the onboard LED on most dev
boards; set to 255 to disable):

- 2 slow blinks -- zeroed
- 5 fast blinks -- refused

A tare is refused if the cranks are still turning (>5 rpm) or if the correction
exceeds 1 kp, which means the brake was not at its lowest setting. Both leave the
previous offset untouched.

If your board has a spare button, set `TARE_BUTTON_PIN` in `BoardConfig.h` and it
will zero on a single press, leaving the calibration button for calibration.

### Web UI

The **Zero / Tare** card shows the current offset in millivolts and offers
*Zero Now* and *Clear Offset*. Equivalent endpoints: `POST /api/tare` and
`POST /api/tare/reset`.

## File Structure

- `wroom-monark.ino`: Main entry point. Handles setup, the main loop, and coordinates components.
- `BleCps.h/cpp`: Handles BLE advertising and notifications using the Cycling Power Service.
- `PowerSimulator.h/cpp`: Generates fake cycling data for testing.
- `LcdUi1602.h/cpp`: Manages the I2C LCD display.
- `PowerSource.h`: Abstract base class for power data sources.
- `PowerSample.h`: Data structure for passing cycling metrics.
- `Calibration.h/cpp`: ADC-to-kp curve, including the zero offset (tare).
- `CalibrationProcess.h/cpp`: Button handling, the calibration wizard and taring.
- `PowerWebServer.h/cpp`: Web UI and JSON API for power, calibration and zeroing.

## Usage

1. Install the required libraries in your Arduino IDE or PlatformIO.
2. Flash the code to your ESP32.
3. The device will start advertising as **"LT2-PowerSim"**.
4. Connect to it using a BLE scanner or a cycling app.
5. The LCD will display the simulated or measured values.
