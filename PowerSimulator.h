#pragma once
#include "PowerSource.h"

class PowerSimulator : public PowerSource {
public:
  explicit PowerSimulator(float cycleConstant = 1.05f);

  void begin() override;
  void update(uint32_t now_ms) override;
  bool hasSample() const override;
  PowerSample getSample() override;

private:
  float cycle_constant;
  uint32_t last_ms = 0;
  PowerSample sample{};
  bool ready = false;

  float rev_accum = 0.0f;
};
