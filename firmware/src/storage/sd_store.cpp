#include "sd_store.h"
#include <STM32FreeRTOS.h>

static SemaphoreHandle_t mutex = nullptr;
static bool cardOk = false;
static uint32_t photoCounter = 0;

bool SdStore::begin() {
  if (!mutex) mutex = xSemaphoreCreateRecursiveMutex();
  cardOk = SD.begin();
  if (!cardOk) {
    Serial.println("[sd] no card (voice prompts fall back to beeps, photos disabled)");
    return false;
  }
  if (!SD.exists("/photos")) SD.mkdir("/photos");
  // Continue numbering after existing photos.
  char name[32];
  while (photoCounter < 99999) {
    snprintf(name, sizeof(name), "/photos/img%05lu.jpg", (unsigned long)photoCounter);
    if (!SD.exists(name)) break;
    photoCounter++;
  }
  Serial.printf("[sd] card ready, next photo #%lu\n", (unsigned long)photoCounter);
  return true;
}

bool SdStore::ready() { return cardOk; }

void SdStore::lock() {
  if (mutex && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
}
void SdStore::unlock() {
  if (mutex && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) xSemaphoreGiveRecursive(mutex);
}

static bool writeFile(const char *path, const uint8_t *a, uint32_t alen, const uint8_t *b, uint32_t blen) {
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  bool ok = true;
  if (alen) ok = f.write(a, alen) == alen;
  if (ok && blen) ok = f.write(b, blen) == blen;
  f.close();
  return ok;
}

bool SdStore::savePhoto(const uint8_t *jpeg, uint32_t len, char *nameOut, size_t nameLen) {
  if (!cardOk || !len) return false;
  Guard g;
  snprintf(nameOut, nameLen, "/photos/img%05lu.jpg", (unsigned long)photoCounter++);
  return writeFile(nameOut, jpeg, len, nullptr, 0);
}

bool SdStore::savePgm(const uint8_t *gray, int w, int h, char *nameOut, size_t nameLen) {
  if (!cardOk) return false;
  Guard g;
  char header[32];
  int hl = snprintf(header, sizeof(header), "P5\n%d %d\n255\n", w, h);
  snprintf(nameOut, nameLen, "/photos/ai%05lu.pgm", (unsigned long)photoCounter++);
  return writeFile(nameOut, (const uint8_t *)header, hl, gray, (uint32_t)w * h);
}
