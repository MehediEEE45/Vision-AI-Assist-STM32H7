#pragma once
#include <Arduino.h>

// Three VL53L0X sensors angled left / centre / right, re-addressed at boot.
namespace TofArray {
constexpr uint16_t kNoTarget = 0xFFFF;   // nothing within range or invalid reading

bool begin();                       // returns true if at least one sensor works
void update();                      // read all sensors (call every TOF_PERIOD_MS)
uint16_t distanceMm(uint8_t side);  // filtered distance, or kNoTarget
bool ok(uint8_t side);              // sensor present
}  // namespace TofArray
