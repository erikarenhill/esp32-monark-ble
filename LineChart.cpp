#include "LineChart.h"

LineChart::LineChart() {
    clear();
}

void LineChart::setTimeWindow(uint32_t windowMs) {
    _timeWindowMs = windowMs;
    _needsRecalc = true;
}

void LineChart::setPosition(int x, int y, int w, int h) {
    _x = x;
    _y = y;
    _w = w;
    _h = h;
}

void LineChart::setColors(uint16_t lineColor, uint16_t gridColor, uint16_t bgColor) {
    _lineColor = lineColor;
    _gridColor = gridColor;
    _bgColor = bgColor;
}

void LineChart::setPadding(float paddingPercent) {
    _paddingPercent = paddingPercent;
}

void LineChart::setGridLines(int xLines, int yLines) {
    _xGridLines = xLines;
    _yGridLines = yLines;
}

void LineChart::clear() {
    _pointCount = 0;
    _headIndex = 0;
    _minValue = 0;
    _maxValue = 100;
    _needsRecalc = true;
}

void LineChart::addPoint(uint32_t timeMs, float value) {
    // Add to circular buffer
    _points[_headIndex].timeMs = timeMs;
    _points[_headIndex].value = value;

    _headIndex = (_headIndex + 1) % MAX_POINTS;
    if (_pointCount < MAX_POINTS) {
        _pointCount++;
    }

    _needsRecalc = true;
}

void LineChart::pruneOldPoints(uint32_t currentTime) {
    if (_pointCount == 0) return;

    uint32_t cutoffTime = (currentTime > _timeWindowMs) ? (currentTime - _timeWindowMs) : 0;

    // Find oldest point still in window
    int validCount = 0;
    for (int i = 0; i < _pointCount; i++) {
        int idx = (_headIndex - _pointCount + i + MAX_POINTS) % MAX_POINTS;
        if (_points[idx].timeMs >= cutoffTime) {
            validCount = _pointCount - i;
            break;
        }
    }

    // Keep only valid points (don't actually remove, just track count)
    // The drawing will only consider points within the time window
}

void LineChart::recalculateMinMax() {
    if (_pointCount == 0) {
        _minValue = 0;
        _maxValue = 100;
        _needsRecalc = false;
        return;
    }

    uint32_t now = 0;
    // Find the latest time
    for (int i = 0; i < _pointCount; i++) {
        int idx = (_headIndex - _pointCount + i + MAX_POINTS) % MAX_POINTS;
        if (_points[idx].timeMs > now) {
            now = _points[idx].timeMs;
        }
    }

    uint32_t cutoffTime = (now > _timeWindowMs) ? (now - _timeWindowMs) : 0;

    float minVal = 999999;
    float maxVal = 0;
    int validPoints = 0;

    for (int i = 0; i < _pointCount; i++) {
        int idx = (_headIndex - _pointCount + i + MAX_POINTS) % MAX_POINTS;
        if (_points[idx].timeMs >= cutoffTime) {
            float v = _points[idx].value;
            if (v > maxVal) maxVal = v;
            if (v < minVal) minVal = v;
            validPoints++;
        }
    }

    if (validPoints == 0) {
        _minValue = 0;
        _maxValue = 100;
    } else {
        // Add 50W padding above and below
        _maxValue = maxVal + 50;
        _minValue = (minVal > 50) ? (minVal - 50) : 0;
    }

    _needsRecalc = false;
}

int LineChart::mapX(uint32_t timeMs, uint32_t now) {
    uint32_t startTime = (now > _timeWindowMs) ? (now - _timeWindowMs) : 0;
    if (timeMs < startTime) return _x;

    float ratio = (float)(timeMs - startTime) / (float)_timeWindowMs;
    return _x + (int)(ratio * _w);
}

int LineChart::mapY(float value) {
    if (_maxValue == _minValue) return _y + _h / 2;

    float ratio = (value - _minValue) / (_maxValue - _minValue);
    // Invert Y (0 at bottom, max at top)
    return _y + _h - (int)(ratio * _h);
}

void LineChart::draw(Adafruit_GFX* gfx) {
    if (_needsRecalc) {
        recalculateMinMax();
    }

    // Clear background
    gfx->fillRect(_x, _y, _w, _h, _bgColor);

    // Draw border
    gfx->drawRect(_x, _y, _w, _h, _gridColor);

    // Draw horizontal grid lines
    for (int i = 1; i < _yGridLines; i++) {
        int gy = _y + (i * _h / _yGridLines);
        gfx->drawFastHLine(_x + 1, gy, _w - 2, _gridColor);
    }

    // Draw vertical grid lines
    for (int i = 1; i < _xGridLines; i++) {
        int gx = _x + (i * _w / _xGridLines);
        gfx->drawFastVLine(gx, _y + 1, _h - 2, _gridColor);
    }

    if (_pointCount < 2) return;

    // Find current time (latest point)
    uint32_t now = 0;
    for (int i = 0; i < _pointCount; i++) {
        int idx = (_headIndex - _pointCount + i + MAX_POINTS) % MAX_POINTS;
        if (_points[idx].timeMs > now) {
            now = _points[idx].timeMs;
        }
    }

    uint32_t cutoffTime = (now > _timeWindowMs) ? (now - _timeWindowMs) : 0;

    // Draw line segments
    int prevX = -1, prevY = -1;
    bool firstPoint = true;

    for (int i = 0; i < _pointCount; i++) {
        int idx = (_headIndex - _pointCount + i + MAX_POINTS) % MAX_POINTS;

        if (_points[idx].timeMs < cutoffTime) continue;

        int px = mapX(_points[idx].timeMs, now);
        int py = mapY(_points[idx].value);

        // Clamp to chart bounds
        if (px < _x) px = _x;
        if (px > _x + _w) px = _x + _w;
        if (py < _y) py = _y;
        if (py > _y + _h) py = _y + _h;

        if (!firstPoint && prevX >= 0) {
            gfx->drawLine(prevX, prevY, px, py, _lineColor);
        }

        prevX = px;
        prevY = py;
        firstPoint = false;
    }

    // Draw min/max labels
    gfx->setTextSize(1);
    gfx->setTextColor(_lineColor);

    // Max value (top)
    gfx->setCursor(_x + 2, _y + 2);
    gfx->print((int)_maxValue);
    gfx->print("W");

    // Min value (bottom) - always 0
    gfx->setCursor(_x + 2, _y + _h - 10);
    gfx->print("0W");
}
