#pragma once
// ---------------------------------------------------------------------------
// Vision AI Assist — board pins and tuning constants
// Board: WeAct Studio MiniSTM32H743VITX (STM32H743VIT6, 25 MHz HSE, 480 MHz)
// Pins marked "board" are fixed by the WeAct PCB (from WeAct's own examples).
// Pins marked "ours" are free pins chosen for the add-on modules.
// ---------------------------------------------------------------------------
#include <Arduino.h>

// ---- Camera: OV3660 on the 24-pin DVP header (board) ----------------------
// DCMI: D0 PC6, D1 PC7, D2 PE0, D3 PE1, D4 PE4, D5 PD3, D6 PE5, D7 PE6,
//       HSYNC PA4, VSYNC PB7, PIXCLK PA6 — all AF13. Configured in dcmi_capture.cpp.
#define CAM_SCCB_SDA        PB9      // I2C1 (board)
#define CAM_SCCB_SCL        PB8      // I2C1 (board)
#define CAM_SCCB_ADDR       0x3C     // OV3660 7-bit address
// XCLK = MCO1 on PA8 = HSI48 / 4 = 12 MHz (same as WeAct's example)

// DCMI sync polarity. The ported OV3660 defaults (reg 0x4740 = 0x21) drive
// VSYNC active-high and HREF active-high. DCMI polarity = level when data is NOT valid.
// If the picture is shifted or never arrives, flip these two first.
#define CAM_DCMI_VSPOLARITY DCMI_VSPOLARITY_HIGH
#define CAM_DCMI_HSPOLARITY DCMI_HSPOLARITY_LOW

// AI frames: 320x240 grayscale (Y8) = 76.8 KB, kept in AXI SRAM
#define CAM_AI_WIDTH        320
#define CAM_AI_HEIGHT       240
// The AI looks at three AI_CROP_SIZE x AI_CROP_SIZE squares (left / centre / right) taken from the
// middle rows of the frame; 160 px gives clear left/right separation (tested in tools/test_model_pc.py).
#define AI_CROP_SIZE        160
// Photo mode: 2048x1536 JPEG into a 256 KB buffer in D2 SRAM (0x30000000)
#define CAM_PHOTO_WIDTH     2048
#define CAM_PHOTO_HEIGHT    1536
#define CAM_JPEG_BUF_SIZE   (256 * 1024)
#define CAM_JPEG_QUALITY    20       // OV3660 QS 0..63, higher = smaller file

// ---- ToF: 3 x VL53L0X on I2C2 (ours) --------------------------------------
#define TOF_SDA             PB11     // I2C2 = default Wire on this variant
#define TOF_SCL             PB10
#define TOF_XSHUT_LEFT      PD5
#define TOF_XSHUT_CENTRE    PD8
#define TOF_XSHUT_RIGHT     PD9
#define TOF_ADDR_LEFT       0x30
#define TOF_ADDR_CENTRE     0x31
#define TOF_ADDR_RIGHT      0x32
#define TOF_MAX_MM          2000     // ignore readings beyond this
#define TOF_PERIOD_MS       33       // ~30 Hz

// ---- Haptics: DRV2605L + LRA on I2C2 (ours) -------------------------------
#define HAPTIC_I2C_ADDR     0x5A

// ---- Audio: MAX98357A on I2S2 (ours) --------------------------------------
// PB12 = I2S2_WS, PB13 = I2S2_CK, PB15 = I2S2_SDO, all AF5. Configured in audio_player.cpp
#define AUDIO_SAMPLE_RATE   16000    // prompts must be 16 kHz, 16-bit, mono WAV

// ---- LCD: 0.96" ST7735 160x80 on SPI4 (board) -----------------------------
#define LCD_MOSI            PE14
#define LCD_SCK             PE12
#define LCD_CS              PE11
#define LCD_DC              PE13
#define LCD_BL              PE10     // backlight, active LOW (from WeAct lcd.c)

// ---- Buttons and LED ------------------------------------------------------
#define BTN_DESCRIBE        PC13     // K1 (board), active HIGH
#define BTN_MUTE            PD0      // ours, to GND, active LOW
#define BTN_VOLUME          PD1      // ours, to GND, active LOW
#define LED_STATUS          PE3      // board LED
#define BATTERY_ADC         PC0      // ours, via 1:2 divider
#define LONG_PRESS_MS       800

// ---- Alert and fusion rules ------------------------------------------------
#define DANGER_CENTRE_MM    1000     // centre obstacle closer than this -> strong vibration
#define DANGER_SIDE_MM      600      // side obstacle closer than this -> vibration
#define AI_TRIGGER_MM       1800     // run the camera + AI when something is this close
#define AI_IDLE_PERIOD_MS   3000     // otherwise still run the AI this often
#define AI_SCORE_THRESHOLD  0.85f    // min class probability to announce (people score 0.9+)
#define REPEAT_HOLDOFF_MS   5000     // do not repeat the same message within this time
