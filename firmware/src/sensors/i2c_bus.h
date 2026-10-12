#pragma once
#include <Wire.h>
#include <STM32FreeRTOS.h>

// I2C2 (PB11 SDA, PB10 SCL) is shared by the three VL53L0X sensors and the DRV2605L.
// Every access from a task must hold this lock.
namespace I2CBus {
void begin();
TwoWire &wire();
void lock();
void unlock();

struct Guard {
  Guard() { lock(); }
  ~Guard() { unlock(); }
};
}  // namespace I2CBus
