#pragma once
#include <Arduino.h>
#include <STM32SD.h>

// microSD on SDMMC1 (4-bit). All access goes through this lock because the
// audio task (reading prompts) and the camera task (saving photos) share the card.
namespace SdStore {
bool begin();
bool ready();
void lock();
void unlock();
bool savePhoto(const uint8_t *jpeg, uint32_t len, char *nameOut, size_t nameLen);
bool savePgm(const uint8_t *gray, int w, int h, char *nameOut, size_t nameLen);  // debug frames

struct Guard {
  Guard() { lock(); }
  ~Guard() { unlock(); }
};
}  // namespace SdStore
