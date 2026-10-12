// Vision AI Assist — entry point. All work happens in FreeRTOS tasks (src/app/vision_app.cpp).
#include <Arduino.h>
#include "app/vision_app.h"

void setup() { VisionApp::start(); }

void loop() {}  // never reached: the FreeRTOS scheduler owns the CPU
