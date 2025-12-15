#pragma once
#include "PowerSample.h"

class IDisplay {
public:
    virtual ~IDisplay() {}
    
    // Initialize the display hardware
    virtual void begin() = 0;
    
    // Show the main power screen
    virtual void showPower(const PowerSample& s, float cycleConstant) = 0;
    
    // Show a 2-line message (used for calibration/status)
    // line1 and line2 can be null if only one line needs updating, 
    // but typically we update both for the 1602.
    virtual void showMessage(const char* line1, const char* line2) = 0;

    // Interaction
    virtual void update() {} // Poll touch/buttons
    virtual bool isActionRequested() { return false; } // Returns true if "Action" (Menu/Next) was pressed
};
