#pragma once
#include <Arduino.h>

// Runs the TFLite Micro classifier on three square crops of the camera frame
// (left / centre / right), matching the three VL53L0X sensors.
enum Side : uint8_t { SIDE_LEFT = 0, SIDE_CENTRE = 1, SIDE_RIGHT = 2, SIDE_COUNT = 3 };

struct SideResult {
  int classIndex;      // best class (may be AI_BACKGROUND_INDEX)
  float score;         // probability 0..1
};

struct Detection {
  SideResult side[SIDE_COUNT];
  uint32_t inferenceMs;  // total time for the three crops
};

namespace Detector {
bool begin();                                             // load model, allocate tensors
bool run(const uint8_t *gray, int w, int h, Detection &out);
int inputSize();                                          // model input width (= height)
size_t arenaUsed();
const char *label(int index);
}  // namespace Detector
