#include "display.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include "config.h"
#include "sensors/tof_array.h"

namespace {
// SPI4 (PE14 MOSI, PE12 SCK); the LCD has no MISO line.
SPIClass lcdSpi(LCD_MOSI, PNUM_NOT_DEFINED, LCD_SCK);
Adafruit_ST7735 tft(&lcdSpi, LCD_CS, LCD_DC, -1);
bool ready = false;
constexpr int kThumbW = 80, kThumbH = 60;
uint16_t thumb[kThumbW * kThumbH];

uint16_t gray565(uint8_t g) { return ((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3); }

void bar(int y, const char *label, uint16_t mm) {
  const int x = 84, w = 74;
  tft.fillRect(x, y, w, 9, ST77XX_BLACK);
  tft.setCursor(x, y + 1);
  tft.setTextColor(ST77XX_WHITE);
  tft.print(label);
  if (mm == TofArray::kNoTarget) {
    tft.print(" --");
    return;
  }
  int len = (int)(w - 14) * (TOF_MAX_MM - mm) / TOF_MAX_MM;
  uint16_t col = mm < DANGER_CENTRE_MM ? ST77XX_RED : (mm < 1500 ? ST77XX_YELLOW : ST77XX_GREEN);
  tft.fillRect(x + 8, y + 1, len, 7, col);
}
}  // namespace

bool Display::begin() {
  pinMode(LCD_BL, OUTPUT);
  backlight(true);
  // WeAct 0.96" = HannStar panel, offsets 26/1 and inverted colours -> MINI160x80_PLUGIN.
  // If colours look wrong or the image is shifted, try INITR_MINI160x80 instead.
  tft.initR(INITR_MINI160x80_PLUGIN);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  ready = true;
  return true;
}

void Display::backlight(bool on) { digitalWrite(LCD_BL, on ? LOW : HIGH); }

void Display::message(const char *l1, const char *l2) {
  if (!ready) return;
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(2, 24);
  tft.print(l1);
  if (l2) {
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(2, 40);
    tft.print(l2);
  }
}

void Display::preview(const uint8_t *gray, int w, int h) {
  if (!ready) return;
  for (int y = 0; y < kThumbH; y++) {
    const uint8_t *row = gray + (y * h / kThumbH) * w;
    for (int x = 0; x < kThumbW; x++) thumb[y * kThumbW + x] = gray565(row[x * w / kThumbW]);
  }
  tft.drawRGBBitmap(0, 0, thumb, kThumbW, kThumbH);
  // Outline of the area the AI looks at (three AI_CROP_SIZE squares across the middle rows).
  const int cropH = AI_CROP_SIZE * kThumbH / h;
  tft.drawRect(0, (kThumbH - cropH) / 2, kThumbW, cropH, ST77XX_BLUE);
  tft.drawFastVLine(kThumbW / 2, (kThumbH - cropH) / 2, cropH, ST77XX_BLUE);
}

void Display::status(uint16_t l, uint16_t c, uint16_t r, const char *det, uint32_t ms, bool muted) {
  if (!ready) return;
  tft.setTextSize(1);
  bar(2, "L", l);
  bar(14, "C", c);
  bar(26, "R", r);
  tft.fillRect(84, 40, 76, 9, ST77XX_BLACK);
  tft.setCursor(84, 41);
  tft.setTextColor(ST77XX_CYAN);
  tft.printf("AI %lums", (unsigned long)ms);
  if (muted) { tft.setTextColor(ST77XX_RED); tft.print(" M"); }
  tft.fillRect(0, 62, 160, 18, ST77XX_BLACK);
  tft.setCursor(2, 66);
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(det ? det : "");
}
