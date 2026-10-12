#include "i2c_bus.h"
#include "config.h"

static SemaphoreHandle_t mutex = nullptr;

void I2CBus::begin() {
  if (!mutex) mutex = xSemaphoreCreateRecursiveMutex();
  Wire.setSDA(TOF_SDA);
  Wire.setSCL(TOF_SCL);
  Wire.begin();
  Wire.setClock(400000);
}

TwoWire &I2CBus::wire() { return Wire; }

void I2CBus::lock() {
  if (mutex && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
}

void I2CBus::unlock() {
  if (mutex && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) xSemaphoreGiveRecursive(mutex);
}
