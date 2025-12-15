#pragma once
#include "PowerSample.h"

class BleCps {
public:
  void begin(const char* deviceName);
  void notify(const PowerSample& s);

private:
  bool started = false;
};
