# TFT Display Wiring Guide for ESP32-WROOM

This document describes the wiring between the ILI9341 TFT display (with NS2009 touch controller) and the ESP32-WROOM.

## Display Specifications

| Property | Value |
|----------|-------|
| Display Controller | ILI9341 |
| Resolution | 240 x 320 pixels |
| Color Depth | 16-bit RGB565 |
| Touch Controller | NS2009 (I2C) |
| Touch I2C Address | 0x48 |

## Wiring Diagram

### TFT Display SPI Connection

```
    ILI9341 TFT                    ESP32-WROOM
    +-----------+                  +-----------+
    |           |                  |           |
    |    VCC    |------------------|  3.3V     |
    |    GND    |------------------|  GND      |
    |    CS     |------------------|  GPIO 5   |  (VSPI CS0)
    |    RST    |------------------|  3.3V/EN  |  (or ESP32 EN pin)
    |    DC     |------------------|  GPIO 4   |
    |    MOSI   |------------------|  GPIO 23  |  (VSPI MOSI)
    |    SCK    |------------------|  GPIO 18  |  (VSPI SCK)
    |    LED/BL |------------------|  3.3V     |  (Backlight always on)
    |    MISO   |------ N/C -------|           |  (Not used)
    |           |                  |           |
    +-----------+                  +-----------+
```

### Touch Panel I2C Connection (NS2009)

```
    NS2009 Touch                   ESP32-WROOM
    +-----------+                  +-----------+
    |           |                  |           |
    |    VCC    |------------------|  3.3V     |
    |    GND    |------------------|  GND      |
    |    SDA    |------------------|  GPIO 21  |  (Default I2C SDA)
    |    SCL    |------------------|  GPIO 22  |  (Default I2C SCL)
    |           |                  |           |
    +-----------+                  +-----------+
```

## Pin Reference Table

| TFT Pin | ESP32 GPIO | Function | Notes |
|---------|------------|----------|-------|
| VCC | 3.3V | Power | Use 3.3V, NOT 5V |
| GND | GND | Ground | |
| CS | GPIO 5 | Chip Select | VSPI CS0, active LOW |
| RST | 3.3V or EN | Reset | Connect externally (not GPIO controlled) |
| DC | GPIO 4 | Data/Command | HIGH=Data, LOW=Command |
| MOSI | GPIO 23 | SPI Data Out | VSPI MOSI |
| SCK | GPIO 18 | SPI Clock | VSPI SCK |
| LED/BL | 3.3V | Backlight | Always on |
| MISO | N/C | SPI Data In | Not used (write-only) |

| Touch Pin | ESP32 GPIO | Function | Notes |
|-----------|------------|----------|-------|
| VCC | 3.3V | Power | |
| GND | GND | Ground | |
| SDA | GPIO 21 | I2C Data | Default ESP32 I2C SDA |
| SCL | GPIO 22 | I2C Clock | Default ESP32 I2C SCL |

## Important Notes

### Why These Pins Were Chosen

The pin selection avoids ESP32 "strapping pins" (GPIO 0, 2, 12, 15) that can interfere with boot:

- **GPIO 12**: Strapping pin for flash voltage - avoided
- **GPIO 15**: Strapping pin for boot messages - avoided
- **GPIO 5, 18, 23**: Standard VSPI pins, safe for boot
- **GPIO 4**: Safe general-purpose GPIO

### RST Pin Configuration

The TFT RST pin is not controlled by GPIO (`TFT_RST = -1` in code). You must:
- Connect RST to 3.3V for always-enabled operation, OR
- Connect RST to ESP32 EN pin so display resets with the MCU

**Do NOT leave RST floating** - this may cause display initialization failures.

### Hardware SPI

The implementation uses **Hardware SPI** for best performance:

```cpp
tft = new Adafruit_ILI9341(TFT_CS, TFT_DC);
```

Hardware SPI automatically uses the default VSPI pins (MOSI=GPIO23, SCK=GPIO18).

### Touch Calibration

The NS2009 touch controller has hardcoded calibration values:

| Parameter | Value |
|-----------|-------|
| CALIBRATE_MIN_X | 200 |
| CALIBRATE_MAX_X | 3850 |
| CALIBRATE_MIN_Y | 400 |
| CALIBRATE_MAX_Y | 3920 |

If touch coordinates are inaccurate, you can:
1. Run the `ts->Calibrate()` function to recalibrate interactively
2. Call `ts->Calibrate(minX, maxX, minY, maxY)` with custom values

## Quick Wiring Checklist

- [ ] VCC to 3.3V (both display and touch)
- [ ] GND to GND (both display and touch)
- [ ] CS to GPIO 5
- [ ] DC to GPIO 4
- [ ] MOSI to GPIO 23
- [ ] SCK to GPIO 18
- [ ] RST to 3.3V or EN pin
- [ ] LED/BL to 3.3V
- [ ] Touch SDA to GPIO 21
- [ ] Touch SCL to GPIO 22

## Troubleshooting

| Problem | Possible Cause | Solution |
|---------|---------------|----------|
| White/blank screen | RST floating | Connect RST to 3.3V |
| No display output | Wrong pins | Verify all SPI connections |
| Display flickers | Power issue | Add 10uF capacitor near VCC |
| Touch not working | I2C not found | Check SDA/SCL connections, verify 0x48 address |
| Wrong touch coordinates | Calibration | Run calibration or adjust values |
| ESP32 won't boot | Strapping pin conflict | Ensure GPIO 12/15 are not connected |

## Code Reference

The TFT implementation is in:
- `TftUi.h` - Class definition
- `TftUi.cpp` - Pin definitions and display logic (lines 3-11 for pin config)
- `NS2009.h` / `NS2009.cpp` - Touch controller driver
