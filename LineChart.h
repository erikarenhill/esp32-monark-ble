#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>

class LineChart {
public:
    static const int MAX_POINTS = 120;  // Max data points to store

    struct DataPoint {
        uint32_t timeMs;
        float value;
    };

    LineChart();

    // Configuration
    void setTimeWindow(uint32_t windowMs);      // X-axis time range (e.g., 60000 for 1 minute)
    void setPosition(int x, int y, int w, int h); // Chart position and size
    void setColors(uint16_t lineColor, uint16_t gridColor, uint16_t bgColor);
    void setPadding(float paddingPercent);       // Y-axis padding (default 10%)
    void setGridLines(int xLines, int yLines);   // Number of grid lines

    // Data
    void addPoint(uint32_t timeMs, float value);
    void clear();

    // Rendering
    void draw(Adafruit_GFX* gfx);

    // Getters
    float getMinValue() const { return _minValue; }
    float getMaxValue() const { return _maxValue; }
    int getPointCount() const { return _pointCount; }

private:
    DataPoint _points[MAX_POINTS];
    int _pointCount = 0;
    int _headIndex = 0;  // Circular buffer head

    // Configuration
    uint32_t _timeWindowMs = 60000;  // Default 1 minute
    int _x = 0, _y = 0, _w = 200, _h = 100;
    uint16_t _lineColor = 0x07E0;    // Green
    uint16_t _gridColor = 0x4208;    // Dark gray
    uint16_t _bgColor = 0x0000;      // Black
    float _paddingPercent = 0.10f;   // 10%
    int _xGridLines = 4;
    int _yGridLines = 3;

    // Cached values
    float _minValue = 0;
    float _maxValue = 100;
    bool _needsRecalc = true;

    void recalculateMinMax();
    void pruneOldPoints(uint32_t currentTime);
    int mapX(uint32_t timeMs, uint32_t now);
    int mapY(float value);
};
