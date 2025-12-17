#include "Workout.h"

Workout::Workout() {
    _state = STOPPED;
}

void Workout::start() {
    if (_state == STOPPED) {
        _startTime = millis();
        _lapStartTime = _startTime;
        _totalPausedMs = 0;
        _lapPausedMs = 0;
        _totalPowerSum = 0;
        _totalSampleCount = 0;
        _lapPowerSum = 0;
        _lapSampleCount = 0;
        _lapCount = 0;
        _state = RUNNING;
    }
}

void Workout::pause() {
    if (_state == RUNNING) {
        _pausedTime = millis();
        _state = PAUSED;
    }
}

void Workout::resume() {
    if (_state == PAUSED) {
        uint32_t pauseDuration = millis() - _pausedTime;
        _totalPausedMs += pauseDuration;
        _lapPausedMs += pauseDuration;
        _state = RUNNING;
    }
}

void Workout::stop() {
    _state = STOPPED;
}

void Workout::lap() {
    if (_state != RUNNING || _lapCount >= MAX_LAPS) return;

    // Save current lap
    LapData& lap = _laps[_lapCount];
    lap.durationMs = getCurrentLapMs();
    lap.totalPowerSum = _lapPowerSum;
    lap.sampleCount = _lapSampleCount;
    lap.avgPower = (_lapSampleCount > 0) ? (_lapPowerSum / _lapSampleCount) : 0;

    _lapCount++;

    // Reset lap stats
    _lapStartTime = millis();
    _lapPausedMs = 0;
    _lapPowerSum = 0;
    _lapSampleCount = 0;
}

void Workout::addPowerSample(float power) {
    if (_state != RUNNING) return;

    _totalPowerSum += power;
    _totalSampleCount++;

    _lapPowerSum += power;
    _lapSampleCount++;
}

uint32_t Workout::getElapsedMs() const {
    if (_state == STOPPED) return 0;

    uint32_t now = (_state == PAUSED) ? _pausedTime : millis();
    return (now - _startTime) - _totalPausedMs;
}

uint32_t Workout::getCurrentLapMs() const {
    if (_state == STOPPED) return 0;

    uint32_t now = (_state == PAUSED) ? _pausedTime : millis();
    return (now - _lapStartTime) - _lapPausedMs;
}

float Workout::getTotalAvgPower() const {
    if (_totalSampleCount == 0) return 0;
    return _totalPowerSum / _totalSampleCount;
}

float Workout::getCurrentLapAvgPower() const {
    if (_lapSampleCount == 0) return 0;
    return _lapPowerSum / _lapSampleCount;
}

const LapData* Workout::getLap(int index) const {
    if (index >= 0 && index < _lapCount) {
        return &_laps[index];
    }
    return nullptr;
}

void Workout::formatTime(uint32_t ms, char* buf, size_t bufSize) {
    uint32_t totalSec = ms / 1000;
    uint32_t hours = totalSec / 3600;
    uint32_t mins = (totalSec % 3600) / 60;
    uint32_t secs = totalSec % 60;

    if (hours > 0) {
        snprintf(buf, bufSize, "%lu:%02lu:%02lu", hours, mins, secs);
    } else {
        snprintf(buf, bufSize, "%02lu:%02lu", mins, secs);
    }
}
