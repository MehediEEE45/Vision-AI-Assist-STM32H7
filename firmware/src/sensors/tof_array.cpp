#include "tof_array.h"
#include <VL53L0X.h>
#include "config.h"
#include "i2c_bus.h"

namespace {
VL53L0X sensors[3];
bool present[3] = {false, false, false};
const uint8_t xshut[3] = {TOF_XSHUT_LEFT, TOF_XSHUT_CENTRE, TOF_XSHUT_RIGHT};
const uint8_t addr[3] = {TOF_ADDR_LEFT, TOF_ADDR_CENTRE, TOF_ADDR_RIGHT};
const char *const names[3] = {"left", "centre", "right"};

// 3-sample median filter per sensor removes single bad readings.
uint16_t history[3][3];
uint8_t histIdx[3] = {0, 0, 0};
volatile uint16_t filtered[3] = {TofArray::kNoTarget, TofArray::kNoTarget, TofArray::kNoTarget};

uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
  if (a > b) { uint16_t t = a; a = b; b = t; }
  if (b > c) { uint16_t t = b; b = c; c = t; }
  if (a > b) { uint16_t t = a; a = b; b = t; }
  return b;
}
}  // namespace

bool TofArray::begin() {
  // Hold every sensor in reset, then wake them one by one and give each a unique address.
  for (int i = 0; i < 3; i++) {
    pinMode(xshut[i], OUTPUT);
    digitalWrite(xshut[i], LOW);
    for (int k = 0; k < 3; k++) history[i][k] = kNoTarget;
  }
  delay(10);
  I2CBus::Guard g;
  int found = 0;
  for (int i = 0; i < 3; i++) {
    digitalWrite(xshut[i], HIGH);
    delay(5);
    VL53L0X &s = sensors[i];
    s.setBus(&I2CBus::wire());
    s.setTimeout(100);
    if (!s.init()) {
      Serial.printf("[tof] %s sensor not found\n", names[i]);
      digitalWrite(xshut[i], LOW);  // keep it off so it cannot clash at 0x29
      continue;
    }
    s.setAddress(addr[i]);
    // Long-range mode (up to ~2 m): lower signal limit, longer VCSEL pulses.
    s.setSignalRateLimit(0.1f);
    s.setVcselPulsePeriod(VL53L0X::VcselPeriodPreRange, 18);
    s.setVcselPulsePeriod(VL53L0X::VcselPeriodFinalRange, 14);
    s.setMeasurementTimingBudget(30000);
    s.startContinuous(0);  // back-to-back measurements
    present[i] = true;
    found++;
    Serial.printf("[tof] %s sensor ready at 0x%02X\n", names[i], addr[i]);
  }
  return found > 0;
}

void TofArray::update() {
  for (int i = 0; i < 3; i++) {
    if (!present[i]) continue;
    uint16_t mm;
    {
      I2CBus::Guard g;
      mm = sensors[i].readRangeContinuousMillimeters();
      if (sensors[i].timeoutOccurred()) mm = kNoTarget;
    }
    // The VL53L0X reports ~8190 mm for "no target".
    if (mm == 0 || mm > TOF_MAX_MM) mm = kNoTarget;
    history[i][histIdx[i]] = mm;
    histIdx[i] = (histIdx[i] + 1) % 3;
    filtered[i] = median3(history[i][0], history[i][1], history[i][2]);
  }
}

uint16_t TofArray::distanceMm(uint8_t side) { return side < 3 ? filtered[side] : kNoTarget; }
bool TofArray::ok(uint8_t side) { return side < 3 && present[side]; }
