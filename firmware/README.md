# Vision AI Assist — Firmware (STM32H743 + PlatformIO)

Offline wearable assistant for visually impaired people, running on the **WeAct Studio STM32H743VIT6** core board:

- **3 × VL53L0X** time-of-flight sensors (left / centre / right) → vibration that speeds up as obstacles get closer
- **OV3660 3 MP camera** → on-device AI (TensorFlow Lite Micro) checks the left, centre and right of the view
- **Voice prompts** from the microSD card through a **MAX98357A** speaker amp: *"Person … on your left … one metre"*
- **DRV2605L** haptics, **0.96" LCD** status screen, **3 MP JPEG photos** saved to microSD

Builds out of the box: `pio run` → RAM 58 %, flash 25 %.

## What runs where

| FreeRTOS task | Priority | Job |
|---|---|---|
| `tofTask` | 4 (highest) | Reads the 3 ToF sensors at 30 Hz; vibrates on danger; wakes the AI when something is within 1.8 m |
| `audioTask` | 3 | Plays queued WAV prompts over I2S DMA (beeps if a file is missing) |
| `aiTask` | 2 | Captures a 320×240 grayscale frame, runs the model on 3 crops, matches each to its ToF sensor, speaks |
| `uiTask` | 1 | Buttons, LCD, status LED, battery check, USB serial commands |

Obstacle warnings never wait for the neural network — `tofTask` pre-empts `aiTask`.

## The AI model

Ships with Google's pretrained **person detection** model (`models/person_detect.tflite`, MobileNetV1, 96×96 grayscale, int8, 300 KB) so the device works immediately.

The frame is split into three 160×160 squares; each is classified separately so the device knows *where* the person is. Checked on a PC with the exact firmware pipeline (`tools/test_model_pc.py`):

| Test image | left | centre | right |
|---|---|---|---|
| person on the left | **person 0.95** | person 0.85 | nothing 0.70 |
| person on the right | nothing 0.70 | nothing 0.51 | **person 0.93** |
| PCB render (no person) | nothing 0.69 | nothing 0.60 | nothing 0.71 |

Announce threshold is 0.85 (`AI_SCORE_THRESHOLD`). Timing on the real board is printed on the serial log (`[ai] NN ms`) — please record it.

**Train your own classes** (door, stairs, car, …): see [`ml/train_classifier.py`](ml/train_classifier.py). It fine-tunes MobileNet on your photos, quantises to int8, and with `--write-firmware` regenerates `src/ai/model_data.cpp` and `src/ai/labels.h`. Collect local photos with the device itself (serial command `s`).

## Wiring

Board pins (camera, LCD, SD card, flash, K1) are fixed by the WeAct PCB. Add-on modules:

| Module | Pin | Board pin |
|---|---|---|
| OV3660 | 24-pin camera header | — (plug in) |
| VL53L0X ×3 (VIN 3.3 V, GND) | SDA / SCL | **PB11 / PB10** (shared) |
| VL53L0X left / centre / right | XSHUT | **PD5 / PD8 / PD9** |
| DRV2605L (VIN 3.3 V, GND) | SDA / SCL | PB11 / PB10 (shared) |
| MAX98357A (VIN 5 V, GND) | LRC / BCLK / DIN | **PB12 / PB13 / PB15** |
| Mute button (other leg → GND) | | **PD0** |
| Volume button (other leg → GND) | | **PD1** |
| Battery + via 100 k / 100 k divider | | **PC0** |
| K1 (on board) | short press = describe, long press = 3 MP photo | PC13 |

Mount the three VL53L0X sensors about **25° apart**, pointing left, straight ahead and right. Most breakouts already have I2C pull-ups.

## Build and flash

```bash
pio run                      # build
pio run -t upload            # flash with ST-Link (SWDIO, SWCLK, GND, 3V3)
pio run -e weact_h743_dfu -t upload   # or USB DFU: hold BOOT0, tap RESET, then upload
pio device monitor           # USB serial log at 115200
```

## microSD card

Format FAT32 and copy `sdcard/prompts/` to the card as `/prompts/`. Photos are saved to `/photos/`.
Regenerate or translate the prompts with `tools/make_prompts.py` (Bangla: `--engine edge --bangla`).

## Serial commands (USB)

| Key | Action |
|---|---|
| `d` | Describe the scene (same as K1) |
| `p` | Take a 3 MP JPEG photo to `/photos/` |
| `s` | Save the current AI frame as `.pgm` to SD (dataset collection) |
| `f` | Send the current frame to the PC → `python tools/frame_viewer.py COM5 --overlay` |

## Bring-up checklist

1. **Log says `cam:ok ai:ok tof:ok`.** If the camera is `X`, check the ribbon and that the OV3660 ID read is `0x3660`.
2. **`[cam] frame timeout`** → swap `CAM_DCMI_VSPOLARITY` / `CAM_DCMI_HSPOLARITY` in `include/config.h`.
3. **Image looks wrong in `frame_viewer.py`** → check polarity again, then `setMirrorFlip()`.
4. **Wrong LCD colours or offset** → use `INITR_MINI160x80` instead of `INITR_MINI160x80_PLUGIN` in `src/ui/display.cpp`.
5. **`[tof] … not found`** → check XSHUT wires; each sensor must be off until it is re-addressed.
6. **No sound** → check MAX98357A power (5 V) and the `/prompts` folder name.
7. **Photo failed** → increase `CAM_JPEG_QUALITY` (smaller file) if the 256 KB buffer overflows.

## Performance notes

- **Instruction cache is on.** The data cache is **off** by default because the SD-card library's DMA is not cache-aware. Turning it on (`SCB_EnableDCache()` in `vision_app.cpp`) roughly doubles AI speed but needs SD buffers moved to non-cacheable memory — test carefully.
- The OV3660 PLL values come from the reference driver tuned for 16 MHz XCLK; this board feeds 12 MHz, so frame rates are ~25 % lower. Fine for AI at a few frames per second.

## Project layout

```
include/config.h          all pins and tuning constants
include/hal_conf_extra.h  enables DCMI + SD HAL modules
src/app/vision_app.cpp    tasks, fusion rules, buttons, photo mode
src/camera/               OV3660 driver (ported from OpenMV/esp32-camera) + DCMI DMA capture
src/ai/                   TFLite Micro wrapper, model C array, labels
src/sensors/              3 x VL53L0X, shared I2C bus lock
src/feedback/             I2S WAV player, DRV2605L haptics
src/storage/              microSD (STM32SD/FatFs)
src/ui/                   ST7735 status screen
models/                   .tflite models
ml/                       training script for your own classes
tools/                    model → C array, PC model test, voice prompt generator, frame viewer
sdcard/prompts/           ready-made English voice prompts
```

## Credits and licences

- OV3660 register tables and driver logic: OpenMV project (MIT), via espressif/esp32-camera (Apache-2.0) — see `ref/`.
- `person_detect.tflite`: TensorFlow Lite Micro examples (Apache-2.0).
- Libraries: STM32duino core, FreeRTOS, FatFs and STM32SD, Chirale TensorFlowLite, Pololu VL53L0X, Adafruit DRV2605 / ST7735 / GFX.
