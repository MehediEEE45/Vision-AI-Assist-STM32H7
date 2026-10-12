#pragma once
#include <Arduino.h>

// DRV2605L + LRA vibration motor.
namespace Haptics {
bool begin();
void pulse(uint8_t effect);   // play one effect from the DRV2605L ROM library
void danger();                // strong double click
void warning();               // single soft click
void ack();                   // short tick for button presses
}  // namespace Haptics
