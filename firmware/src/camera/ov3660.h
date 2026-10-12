#pragma once
#include <Arduino.h>
#include <Wire.h>

// OV3660 3 MP sensor control over SCCB (I2C). Register sequences are ported
// from the OpenMV / esp32-camera driver (see ov3660_settings.h).
class OV3660 {
 public:
  enum class Format { Gray, Yuv422, Rgb565, Jpeg };

  explicit OV3660(TwoWire &bus, uint8_t addr = 0x3C) : bus_(bus), addr_(addr) {}

  bool begin();                         // probe + software reset + default registers
  uint16_t chipId();                    // 0x3660 when the sensor is found
  bool setFormat(Format f);
  bool setFrameSize(uint16_t w, uint16_t h);   // 4:3 sizes up to 2048x1536
  bool setQuality(uint8_t qs);          // JPEG quantisation scale 0..63
  bool setMirrorFlip(bool hmirror, bool vflip);
  bool setAeLevel(int level);           // -5..5
  Format format() const { return format_; }

 private:
  int readReg(uint16_t reg);
  bool writeReg(uint16_t reg, uint8_t val);
  bool writeReg16(uint16_t reg, uint16_t val);
  bool writeAddrReg(uint16_t reg, uint16_t x, uint16_t y);
  bool setRegBits(uint16_t reg, uint8_t mask, bool enable);
  bool writeRegs(const uint16_t (*regs)[2]);
  bool setPll(bool bypass, uint8_t mul, uint8_t sysDiv, uint8_t preDiv, bool root2x,
              uint8_t seld5, bool pclkManual, uint8_t pclkDiv);
  bool applyImageOptions();

  TwoWire &bus_;
  uint8_t addr_;
  Format format_ = Format::Yuv422;
  uint16_t width_ = 0, height_ = 0;
  bool binning_ = false, hmirror_ = false, vflip_ = false;
};
