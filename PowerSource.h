#pragma once
#include "PowerSample.h"

class PowerSource {
public:
  virtual ~PowerSource() {}

  // Called once in setup()
  virtual void begin() = 0;

  // Called every loop iteration
  virtual void update(uint32_t now_ms) = 0;

  // True if a fresh sample is ready
  virtual bool hasSample() const = 0;

  // Fetch latest sample
  virtual PowerSample getSample() = 0;

  // Live-update the cycle constant (Monark cycle calibration scalar). Web UI
  // pushes this on Save so the next sample reflects the new value without a
  // reboot.
  virtual void setCycleConstant(float cc) = 0;
};
