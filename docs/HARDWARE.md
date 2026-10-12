# Hardware Design

## Main board: WeAct Studio STM32H743VIT6 core board

Bought locally from [RoboticsBD](https://store.roboticsbd.com/internet-of-things-iot/3741-weact-studio-stm32h743vit6-development-core-board-with-096-tft-screen-robotics-bangladesh.html). Official docs and examples: [WeActStudio/MiniSTM32H7xx](https://github.com/WeActStudio/MiniSTM32H7xx).

| Feature | Detail | What we use it for |
|---|---|---|
| MCU | STM32H743VIT6, single Cortex-M7, up to 480 MHz, FPU, L1 caches | Everything: camera, AI, sensors, feedback |
| Internal flash | 2 MB | Firmware + INT8 model weights |
| Internal RAM | 1 MB total (512 KB AXI SRAM, 128 KB DTCM, 64 KB ITCM, 288 KB SRAM1–3, 64 KB SRAM4) | Model activations, frame buffers, RTOS |
| QSPI flash | 8 MB on QUADSPI (memory-mapped) | Voice prompts, extra model weights |
| SPI flash | 8 MB on SPI1 | Logs, settings |
| Camera port | 8-bit DCMI, 24-pin header; WeAct supports OV2640, OV7670, OV7725, OV5640-AF | **OV3660 3 MP** (same 24-pin DVP pinout) |
| Display | 0.96" ST7735 TFT (160×80) on SPI4 | Debug preview, status |
| microSD | SDMMC1, 4-bit | Saving 3 MP snapshots, datasets, logs |
| USB | USB-C (OTG FS) | Power, frame streaming to PC (CDC / UVC) |
| Key / LED | K1 on PC13, LED on PE3 | "Describe scene" button, status |
| Clock | 25 MHz HSE, 32.768 kHz LSE | |
| Power | 3.3–5.5 V in, on-board DC-DC (1 A) | |

> **No external SDRAM** and **single core**. That shapes the whole design: camera frames are kept small (QVGA or 240×240) for AI, full 3 MP images are captured only as **JPEG** straight to the SD card, and FreeRTOS priorities (not a second core) keep obstacle warnings fast.

## Add-on modules

| Item | Part | Interface | Notes |
|---|---|---|---|
| Camera | OV3660 module, 3 MP (2048×1536), 24-pin DVP | DCMI + SCCB on I2C1 | JPEG + RGB565/YUV. Fallback: OV5640-AF, already supported by WeAct examples |
| ToF | VL53L5CX breakout | I2C2 + INT + LPn | 8×8 zones, up to 4 m, 63° diagonal FoV |
| Audio amp | MAX98357A breakout | I2S2 (SPI2) | Mono class-D, drives speaker or bone-conduction transducer |
| Haptics | DRV2605L breakout + LRA | I2C2 (shared with ToF) | Addresses differ (0x29 ToF, 0x5A haptics) |
| Buttons | 2 extra tactile buttons | GPIO EXTI | Mute, volume (K1 = describe) |
| Battery | 1-cell LiPo 2000 mAh + TP4056/BQ25895 charger + 5 V boost or 3.3 V buck | Board 5 V input | |

## Pin map

Pins marked **board** come from WeAct's example `.ioc` files (fixed by the PCB). Pins marked **ours** are free pins we chose; check them against the schematic before wiring.

| Function | Pins | Source |
|---|---|---|
| DCMI data D0–D7 | PC6, PC7, PE0, PE1, PE4, PD3, PE5, PE6 | board |
| DCMI HSYNC / VSYNC / PIXCLK | PA4 / PB7 / PA6 | board |
| Camera XCLK | PA8 (MCO1) | board |
| Camera SCCB | I2C1: PB8 SCL, PB9 SDA | board |
| LCD ST7735 | SPI4: PE12 SCK, PE14 MOSI; PE11 CS, PE13 DC, PE10 backlight (TIM1_CH2N) | board |
| microSD | SDMMC1: PC8–PC11 D0–D3, PC12 CK, PD2 CMD | board |
| QSPI flash | PB2 CLK, PB6 NCS, PD11/PD12/PE2/PD13 IO0–3 | board |
| SPI flash | SPI1: PB3 SCK, PB4 MISO, PD7 MOSI, PD6 CS | board |
| USB | PA11 DM, PA12 DP | board |
| Key / LED | PC13 / PE3 | board |
| SWD | PA13 SWDIO, PA14 SWCLK | board |
| ToF + haptics bus | I2C2: PB10 SCL, PB11 SDA | ours |
| ToF INT / LPn | PD4 / PD5 | ours |
| Audio I2S | I2S2: PB12 WS, PB13 CK, PB15 SD | ours |
| Mute / volume buttons | PD0 / PD1 (EXTI) | ours |
| Battery voltage | PC0 (ADC, via 1:2 divider) | ours |

## Memory plan

| Region | Size | Contents |
|---|---|---|
| Internal flash | 2 MB | Firmware (~300 KB) + model weights (≤ 1.5 MB) |
| AXI SRAM | 512 KB | Model activations (≤ 400 KB) + tensor input |
| SRAM1–3 | 288 KB | Camera frame buffer (240×240 RGB565 = 112.5 KB; QVGA = 150 KB) + DMA buffers |
| DTCM | 128 KB | FreeRTOS stacks, fast variables |
| SRAM4 | 64 KB | ToF data, audio DMA buffer |
| QSPI (memory-mapped) | 8 MB | Voice prompts, overflow weights |
| microSD | — | 3 MP JPEG snapshots, logs, dataset capture |

Rule: DMA buffers go in SRAM1–3 or SRAM4 (DTCM is not reachable by DMA1/DMA2), and are placed in a non-cacheable MPU region or cleaned/invalidated around transfers.

## Clock

WeAct examples run at 240 MHz. The H743 (revision V) can run at **480 MHz** with voltage scale 0 — we will use 480 MHz for inference and drop to 240 MHz or lower when idle.

## Power budget (estimate, to be measured)

| Load | Current |
|---|---|
| STM32H743 at 480 MHz running AI | ~150 mA |
| OV3660 streaming | ~100 mA |
| VL53L5CX at 15 Hz | ~95 mA |
| LCD (debug only, off in wearable mode) | ~20 mA |
| Audio + haptics (average) | ~40 mA |
| **Total** | **~400 mA at 3.3 V** |

2000 mAh LiPo → about 4.5 h continuous. Running detection only when ToF sees something or the user presses K1 should reach the **6 h** target.

## Parts list — prototype (approx.)

| Item | Qty | Approx. cost |
|---|---|---|
| WeAct STM32H743VIT6 core board with 0.96" TFT | 1 | check RoboticsBD price |
| OV3660 3 MP DVP module (24-pin) | 1 | ~USD 12 |
| VL53L5CX breakout | 1 | ~USD 20 |
| MAX98357A breakout | 1 | ~USD 6 |
| DRV2605L breakout + LRA | 1 | ~USD 10 |
| microSD card 16 GB | 1 | ~USD 5 |
| LiPo 2000 mAh + charger/boost | 1 | ~USD 12 |
| Speaker or bone-conduction transducer, buttons, wires | — | ~USD 10 |

## Wearable board (Phase 5)

Same STM32H743VIT6 (LQFP100, hand-solderable), so the prototype firmware ports with the same pin map:

- 4-layer PCB in KiCad: MCU, QSPI flash, microSD, camera FPC, VL53L5CX, MAX98357A, DRV2605L, BQ25895 charger, TPS62133 3.3 V buck, USB-C with ESD
- DCMI lines kept short and length-matched, solid ground plane under the camera and clock lines
- Assembled at JLCPCB; 3D-printed chest-clip or glasses-mount enclosure
