#pragma once
#include <Arduino.h>

struct LapData {
    uint32_t durationMs;
    float avgPower;
    float totalPowerSum;
    uint32_t sampleCount;
};

class Workout {
public:
    static const int MAX_LAPS = 50;

    enum State {
        STOPPED,
        RUNNING,
        PAUSED
    };

    Workout();

    void start();
    void pause();
    void resume();
    void stop();
    void lap();

    void addPowerSample(float power);

    // Getters
    State getState() const { return _state; }
    bool isRunning() const { return _state == RUNNING; }
    bool isPaused() const { return _state == PAUSED; }
    bool isStopped() const { return _state == STOPPED; }

    uint32_t getElapsedMs() const;
    uint32_t getCurrentLapMs() const;

    float getTotalAvgPower() const;
    float getCurrentLapAvgPower() const;

    int getLapCount() const { return _lapCount; }
    const LapData* getLap(int index) const;

    // Format time as MM:SS or HH:MM:SS
    static void formatTime(uint32_t ms, char* buf, size_t bufSize);

private:
    State _state = STOPPED;

    uint32_t _startTime = 0;
    uint32_t _pausedTime = 0;
    uint32_t _totalPausedMs = 0;

    uint32_t _lapStartTime = 0;
    uint32_t _lapPausedMs = 0;

    // Total stats
    float _totalPowerSum = 0;
    uint32_t _totalSampleCount = 0;

    // Current lap stats
    float _lapPowerSum = 0;
    uint32_t _lapSampleCount = 0;

    // Completed laps
    LapData _laps[MAX_LAPS];
    int _lapCount = 0;
};
