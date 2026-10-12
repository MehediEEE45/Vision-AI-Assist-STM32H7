#include "haptics.h"
#include <Adafruit_DRV2605.h>
#include "sensors/i2c_bus.h"

static Adafruit_DRV2605 drv;
static bool ready = false;

bool Haptics::begin() {
  I2CBus::Guard g;
  if (!drv.begin(&I2CBus::wire())) {
    Serial.println("[haptics] DRV2605L not found");
    return false;
  }
  drv.useLRA();
  drv.selectLibrary(6);              // library 6 = LRA effects
  drv.setMode(DRV2605_MODE_INTTRIG);
  ready = true;
  Serial.println("[haptics] DRV2605L ready");
  return true;
}

void Haptics::pulse(uint8_t effect) {
  if (!ready) return;
  I2CBus::Guard g;
  drv.setWaveform(0, effect);
  drv.setWaveform(1, 0);
  drv.go();
}

// Effect numbers from the DRV2605L datasheet ROM table.
void Haptics::danger() {
  if (!ready) return;
  I2CBus::Guard g;
  drv.setWaveform(0, 1);    // strong click 100 %
  drv.setWaveform(1, 1);
  drv.setWaveform(2, 0);
  drv.go();
}
void Haptics::warning() { pulse(7); }   // soft bump 100 %
void Haptics::ack() { pulse(24); }      // sharp tick 1
