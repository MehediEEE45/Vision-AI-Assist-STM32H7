#pragma once
#include <Arduino.h>

// DCMI + DMA capture for the 8-bit camera header on the WeAct H743 board.
namespace Dcmi {

bool begin();                       // XCLK on PA8, DCMI pins, DMA, IRQs, D2 SRAM clocks
void xclkOn();                      // 12 MHz camera clock (call before talking to the sensor)
void setJpegMode(bool jpeg);        // reconfigure DCMI for JPEG (no fixed line length)

// Capture one frame into buf (len bytes, multiple of 4). Blocks the calling task
// until the frame ends or timeout. Returns true on success.
bool captureFrame(uint8_t *buf, uint32_t len, uint32_t timeoutMs);

// Buffers
uint8_t *aiFrame();                 // CAM_AI_WIDTH x CAM_AI_HEIGHT grayscale (AXI SRAM)
uint8_t *jpegBuffer();              // CAM_JPEG_BUF_SIZE bytes in D2 SRAM (0x30000000)
uint32_t jpegLength(const uint8_t *buf, uint32_t max);  // find JPEG end marker (FFD9)

}  // namespace Dcmi
