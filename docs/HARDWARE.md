# Hardware Design

## Prototype stack (Phases 1–4)

Built on the **STM32H747I-DISCO** so the team can work on firmware and AI before the custom board exists.

| Item | Part | Interface | Notes |
|---|---|---|---|
| Dev board | STM32H747I-DISCO | — | M7 480 MHz + M4 240 MHz, 2 MB flash, 1 MB SRAM, 32 MB SDRAM, QSPI flash, 4" LCD, camera connector |
| Camera | OV3660 module, 3 MP | DCMI (8-bit DVP) + SCCB (I2C) | 2048×1536 max; JPEG and RGB565/YUV output; use QVGA/VGA for AI |
| ToF | VL53L5CX breakout | I2C (1 MHz) + INT | 8×8 zones, up to 4 m, 63° diagonal FoV |
| Audio amp | MAX98357A | I2S (SAI) | 3.2 W class-D, mono |
| Speaker | 1 W 8 Ω mini speaker or bone-conduction transducer | — | Bone conduction keeps ears free for street sounds |
| Haptics | DRV2605L + 10 mm LRA | I2C | Built-in effect library |
| Input | 3 tactile buttons | GPIO EXTI | Describe / mute / volume |

## Wearable board (Phase 5)

| Block | Part | Why |
|---|---|---|
| MCU | STM32H747XIH6 (TFBGA240) or STM32H743VIT6 (LQFP100) | H747 keeps dual-core firmware; H743 is cheaper and easier to solder if the model fits without SDRAM |
| Memory | IS42S16160J SDRAM 32 MB (only if needed) + W25Q128 QSPI 16 MB | Frame buffers; model weights and voice clips |
| Camera | OV3660 on 24-pin FPC | Same driver as prototype |
| ToF | VL53L5CX (bare module) | Direct on board, cover-glass window in the enclosure |
| Charger | BQ25895 | 1-cell LiPo, USB-C input, power path |
| 3.3 V rail | TPS62133 buck (3 A) | Camera + MCU peaks |
| 1.2 V / 2.8 V | LDOs for camera core and analog | Per OV3660 datasheet |
| USB | USB-C, USBLC6 ESD | Charging + firmware update + debug |
| Debug | Tag-Connect / SWD pads | |

**PCB:** 4-layer (signal / GND / power / signal), DCMI and SDRAM lines length-matched, camera clock kept short, ground plane under all high-speed lines. Designed in KiCad, assembled by JLCPCB.

## Pin plan (prototype, to confirm against the Disco schematic)

| Function | Peripheral | Notes |
|---|---|---|
| Camera data | DCMI D0–D7, HSYNC, VSYNC, PIXCLK | Disco camera connector |
| Camera control | I2C (SCCB), PWDN, RESET, XCLK (MCO) | |
| ToF | I2C4 + EXTI | Separate bus from the camera |
| Haptics | I2C4 (shared with ToF) | |
| Audio | SAI1 (I2S mode) | |
| Buttons | GPIO EXTI × 3 | Arduino header |
| Debug | USART1 / USB-CDC | Frame streaming to PC |

## Power budget (estimate, to be measured)

| Load | Current @ 3.7 V |
|---|---|
| STM32H747 running AI at 480 MHz | ~180 mA |
| OV3660 streaming | ~100 mA |
| VL53L5CX at 15 Hz | ~95 mA |
| SDRAM | ~40 mA |
| Audio (average) | ~30 mA |
| Haptics (average) | ~15 mA |
| **Total (continuous AI)** | **~460 mA** |

2000 mAh / 460 mA ≈ **4.3 h** continuous. To reach the **6 h** target, run object detection only when ToF sees something or the user presses "describe" (≈ 300 mA average → 6.5 h).

## Bill of materials — prototype (approx.)

| Item | Qty | Approx. cost (USD) |
|---|---|---|
| STM32H747I-DISCO | 1 | 90 |
| OV3660 module | 1 | 12 |
| VL53L5CX breakout | 1 | 20 |
| MAX98357A breakout | 1 | 6 |
| DRV2605L breakout + LRA | 1 | 10 |
| LiPo 2000 mAh + charger board | 1 | 12 |
| Speaker / bone conduction, buttons, wires | — | 10 |
| **Total** | | **~160** |
