#pragma once
#include <Arduino.h>

// 0.96" ST7735 (160x80) status screen: camera preview, 3 distance bars, last detection.
namespace Display {
bool begin();
void message(const char *line1, const char *line2 = nullptr);
void preview(const uint8_t *gray, int w, int h);   // draws an 80x60 thumbnail on the left
void status(uint16_t left, uint16_t centre, uint16_t right, const char *detection,
            uint32_t inferenceMs, bool muted);
void backlight(bool on);
}  // namespace Display
