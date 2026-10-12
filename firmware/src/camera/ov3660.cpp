// OV3660 driver for STM32 (Arduino Wire). Logic ported from the OpenMV / esp32-camera
// OV3660 driver (MIT licence, Copyright (c) 2013/2014 Ibrahim Abdelkader).
#include "ov3660.h"
#include "ov3660_regs.h"
#include "ov3660_settings.h"

#include <STM32FreeRTOS.h>

static void sensorDelay(uint32_t ms) {
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) vTaskDelay(pdMS_TO_TICKS(ms));
  else delay(ms);
}

int OV3660::readReg(uint16_t reg) {
  bus_.beginTransmission(addr_);
  bus_.write(reg >> 8);
  bus_.write(reg & 0xFF);
  if (bus_.endTransmission(true) != 0) return -1;
  if (bus_.requestFrom(addr_, (uint8_t)1) != 1) return -1;
  return bus_.read();
}

bool OV3660::writeReg(uint16_t reg, uint8_t val) {
  bus_.beginTransmission(addr_);
  bus_.write(reg >> 8);
  bus_.write(reg & 0xFF);
  bus_.write(val);
  return bus_.endTransmission(true) == 0;
}

bool OV3660::writeReg16(uint16_t reg, uint16_t val) {
  return writeReg(reg, val >> 8) && writeReg(reg + 1, val & 0xFF);
}

bool OV3660::writeAddrReg(uint16_t reg, uint16_t x, uint16_t y) {
  return writeReg16(reg, x) && writeReg16(reg + 2, y);
}

bool OV3660::setRegBits(uint16_t reg, uint8_t mask, bool enable) {
  int v = readReg(reg);
  if (v < 0) return false;
  uint8_t nv = enable ? (v | mask) : (v & ~mask);
  return writeReg(reg, nv);
}

bool OV3660::writeRegs(const uint16_t (*regs)[2]) {
  for (int i = 0; regs[i][0] != REGLIST_TAIL; i++) {
    if (regs[i][0] == REG_DLY) {
      sensorDelay(regs[i][1]);
    } else if (!writeReg(regs[i][0], (uint8_t)regs[i][1])) {
      return false;
    }
  }
  return true;
}

uint16_t OV3660::chipId() {
  int h = readReg(0x300A), l = readReg(0x300B);
  if (h < 0 || l < 0) return 0;
  return (uint16_t)((h << 8) | l);
}

bool OV3660::setPll(bool bypass, uint8_t mul, uint8_t sysDiv, uint8_t preDiv, bool root2x,
                    uint8_t seld5, bool pclkManual, uint8_t pclkDiv) {
  if (mul > 31 || sysDiv > 15 || preDiv > 3 || pclkDiv > 31 || seld5 > 3) return false;
  return writeReg(SC_PLLS_CTRL0, bypass ? 0x80 : 0x00) &&
         writeReg(SC_PLLS_CTRL1, mul & 0x1F) &&
         writeReg(SC_PLLS_CTRL2, 0x10 | (sysDiv & 0x0F)) &&
         writeReg(SC_PLLS_CTRL3, ((preDiv & 0x3) << 4) | seld5 | (root2x ? 0x40 : 0x00)) &&
         writeReg(PCLK_RATIO, pclkDiv & 0x1F) &&
         writeReg(VFIFO_CTRL0C, pclkManual ? 0x22 : 0x20);
}

bool OV3660::setAeLevel(int level) {
  if (level < -5 || level > 5) return false;
  int target = ((level + 5) * 10) + 5;
  int low = target * 23 / 25, high = target * 27 / 25;
  int fastLow = low >> 1, fastHigh = high << 1;
  if (fastHigh > 255) fastHigh = 255;
  return writeReg(0x3a0f, high) && writeReg(0x3a10, low) && writeReg(0x3a1b, high) &&
         writeReg(0x3a1e, low) && writeReg(0x3a11, fastHigh) && writeReg(0x3a1f, fastLow);
}

bool OV3660::begin() {
  bus_.begin();
  bus_.setClock(100000);
  uint16_t id = chipId();
  if (id != 0x3660) {
    Serial.printf("[cam] OV3660 not found (id=0x%04X)\n", id);
    return false;
  }
  if (!writeReg(SYSTEM_CTROL0, 0x82)) return false;  // software reset
  sensorDelay(100);
  if (!writeRegs(sensor_default_regs)) return false;
  setAeLevel(0);
  sensorDelay(100);
  Serial.println("[cam] OV3660 ready");
  return true;
}

bool OV3660::setFormat(Format f) {
  const uint16_t(*regs)[2];
  switch (f) {
    case Format::Gray: regs = sensor_fmt_grayscale; break;
    case Format::Rgb565:    regs = sensor_fmt_rgb565; break;
    case Format::Jpeg:      regs = sensor_fmt_jpeg; break;
    default:                regs = sensor_fmt_yuv422; break;
  }
  if (!writeRegs(regs)) return false;
  format_ = f;
  // Re-apply the frame size so compression and PLL match the new format.
  return width_ ? setFrameSize(width_, height_) : true;
}

bool OV3660::applyImageOptions() {
  uint8_t reg20 = 0, reg21 = 0, test = 0, reg4514 = 0x88;
  if (format_ == Format::Jpeg) reg21 |= 0x20;
  if (binning_) { reg20 |= 0x01; reg21 |= 0x01; test |= 4; } else { reg20 |= 0x40; }
  if (vflip_)   { reg20 |= 0x06; test |= 1; }
  if (hmirror_) { reg21 |= 0x06; test |= 2; }
  static const uint8_t table[8] = {0x88, 0x88, 0xbb, 0xbb, 0xaa, 0xbb, 0xbb, 0xaa};
  reg4514 = table[test];
  bool ok = writeReg(TIMING_TC_REG20, reg20) && writeReg(TIMING_TC_REG21, reg21) &&
            writeReg(0x4514, reg4514);
  if (binning_) {
    ok = ok && writeReg(0x4520, 0x0b) && writeReg(X_INCREMENT, 0x31) && writeReg(Y_INCREMENT, 0x31);
  } else {
    ok = ok && writeReg(0x4520, 0xb0) && writeReg(X_INCREMENT, 0x11) && writeReg(Y_INCREMENT, 0x11);
  }
  return ok;
}

bool OV3660::setMirrorFlip(bool hmirror, bool vflip) {
  hmirror_ = hmirror;
  vflip_ = vflip;
  return applyImageOptions();
}

bool OV3660::setFrameSize(uint16_t w, uint16_t h) {
  if (w > 2048 || h > 1536) return false;
  const ratio_settings_t &s = ratio_table[0];  // 4:3 window, full sensor
  binning_ = (w <= s.max_width / 2 && h <= s.max_height / 2);
  bool scale = !((w == s.max_width && h == s.max_height) ||
                 (w == s.max_width / 2 && h == s.max_height / 2));

  bool ok = writeAddrReg(X_ADDR_ST_H, s.start_x, s.start_y) &&
            writeAddrReg(X_ADDR_END_H, s.end_x, s.end_y) &&
            writeAddrReg(X_OUTPUT_SIZE_H, w, h);
  if (binning_) {
    ok = ok && writeAddrReg(X_TOTAL_SIZE_H, s.total_x, (s.total_y / 2) + 1) &&
         writeAddrReg(X_OFFSET_H, 8, 2);
  } else {
    ok = ok && writeAddrReg(X_TOTAL_SIZE_H, s.total_x, s.total_y) &&
         writeAddrReg(X_OFFSET_H, 16, 6);
  }
  ok = ok && setRegBits(ISP_CONTROL_01, 0x20, scale) && applyImageOptions();
  if (!ok) return false;

  // PLL values from the reference driver (tuned for 16 MHz XCLK; we feed 12 MHz,
  // so pixel clocks are 25 % lower — still well inside the DCMI's range).
  if (format_ == Format::Jpeg) {
    ok = (w == 2048 && h == 1536) ? setPll(false, 24, 1, 3, false, 0, true, 8)
                                  : setPll(false, 30, 1, 3, false, 0, true, 10);
  } else if (w > 480) {
    ok = setPll(false, 4, 1, 0, false, 2, true, 2);
  } else if (w >= 320) {
    ok = setPll(false, 8, 1, 0, false, 2, true, 4);
  } else {
    ok = setPll(false, 8, 1, 0, false, 0, true, 8);
  }
  if (ok) { width_ = w; height_ = h; }
  return ok;
}

bool OV3660::setQuality(uint8_t qs) { return writeReg(COMPRESSION_CTRL07, qs & 0x3F); }
