# Project Plan

Each phase ends with a demo you can show. Each phase is a GitHub **milestone**; each task below is a GitHub **issue**.

## Goals and success criteria

| # | Requirement | Target | How we measure |
|---|---|---|---|
| R1 | Obstacle warning latency | < 200 ms from object entering range to first vibration | Logic analyser on ToF interrupt and haptic pin |
| R2 | Obstacle detection range | 0.2 – 3.5 m, ±5 cm | Tape-measure bench test, 20 points |
| R3 | Object detection | ≥ 5 FPS at 192×192, mAP@0.5 ≥ 0.45 on our test set | On-target timer + held-out test images |
| R4 | Classes | person, door, stairs, chair, table, car, motorbike, bicycle, pole, dog | Dataset definition |
| R5 | Battery life | ≥ 6 h continuous use | Discharge test |
| R6 | Weight (wearable) | < 120 g | Scale |
| R7 | Cost (wearable, 10 units) | < USD 60 BOM | BOM spreadsheet |
| R8 | Privacy | No image leaves the device by default | Firmware review |

---

## Phase 0 — Research and setup · Nov 2026

- [ ] Literature review: 10+ papers on wearable assistive vision and TinyML object detection
- [ ] Interview 3–5 visually impaired people (with consent) about what alerts they actually need
- [ ] Freeze requirements R1–R8
- [ ] Order: STM32H747I-DISCO, OV3660 module, VL53L5CX breakout, MAX98357A, DRV2605L + LRA, LiPo + charger
- [ ] Set up STM32CubeIDE, X-CUBE-AI, STM32CubeMX projects for CM7 and CM4
- [ ] Repository, coding style, branch rules

**Demo:** requirements document and parts on the bench.

## Phase 1 — Camera bring-up · Dec 2026

- [ ] OV3660 driver: SCCB (I2C) register init, power-down and reset lines
- [ ] DCMI + DMA capture into SDRAM (QVGA RGB565 first, then 3 MP JPEG snapshot)
- [ ] Show live preview on the Disco LCD
- [ ] Stream frames to PC over USB-CDC; Python viewer in `tools/`
- [ ] Measure capture FPS and DMA load

**Demo:** live camera preview on the Disco display and on a laptop.

## Phase 2 — ToF obstacle warning · Jan 2027 *(first useful demo)*

- [ ] VL53L5CX ULD driver on the CM4 (I2C, 8×8 at 15 Hz)
- [ ] Obstacle logic: nearest distance per left / centre / right column, ignore floor zones
- [ ] DRV2605L haptics: pulse rate rises as distance falls
- [ ] FreeRTOS tasks on the CM4: sensor, logic, feedback
- [ ] Bench test R1 and R2

**Demo:** walk toward a wall blindfolded and feel the warning.

## Phase 3 — Object detection on the M7 · Feb 2027

- [ ] Dataset: COCO subset for the R4 classes + 1,000+ local photos (SUST campus, Sylhet streets: rickshaws, poles, stairs)
- [ ] Baseline: ST Model Zoo `st_ssd_mobilenet_v1` / `st_yolo_lc_v1`, 192×192
- [ ] Fine-tune, INT8 post-training quantisation, evaluate mAP
- [ ] Export with STM32Cube.AI, check RAM/flash fit (activations in AXI SRAM, weights in internal flash or QSPI)
- [ ] Pre-processing on the M7: crop, resize, normalise with DMA2D
- [ ] Measure FPS and accuracy on target (R3)

**Demo:** boxes drawn on the Disco LCD in real time.

## Phase 4 — Fusion and voice feedback · Mar 2027

- [ ] Inter-core messaging (HSEM + shared SRAM) from M7 to M4
- [ ] Map each box to ToF zones → distance and direction
- [ ] Priority rules: close obstacles first, then people and vehicles, then everything else
- [ ] Voice prompts: record ~40 short clips (English and Bangla), store in QSPI, play with I2S + DMA
- [ ] Buttons: "describe scene", mute, volume
- [ ] Power profiling and low-power modes when idle

**Demo:** full prototype on the Disco board says *"person, two metres, left"*.

## Phase 5 — Wearable hardware · Apr–May 2027

- [ ] Schematic: STM32H747 (or H743 + cheaper SDRAM-free variant if the model fits), camera FPC, ToF, audio, haptics, charger, USB-C
- [ ] 4-layer PCB in KiCad, impedance-controlled DCMI and SDRAM routing, ground planes
- [ ] DRC/ERC clean, order from JLCPCB with assembly
- [ ] 3D-printed chest-clip or glasses-mount enclosure
- [ ] Port firmware to the custom board; bring-up checklist

**Demo:** wearable device running on battery.

## Phase 6 — Field testing · Jun 2027

- [ ] Ethics approval from the department before testing with people
- [ ] Indoor and outdoor obstacle courses
- [ ] Measure detection rate, false alarms, user ratings (SUS questionnaire)
- [ ] Fix the top problems found

**Demo:** test results and user feedback.

## Phase 7 — Publication and release · Jul 2027

- [ ] Conference paper (IEEE format) on the system and results
- [ ] Thesis chapters
- [ ] Clean public release: firmware, model, KiCad files, build guide
- [ ] Demo video

---

## Risks

| Risk | Impact | Plan B |
|---|---|---|
| Model too large or too slow for the H7 | R3 missed | Smaller input (128×128), fewer classes, or STM32N6 (has an NPU) |
| OV3660 driver issues | Phase 1 delay | OV5640 (well documented) or Arducam Mega 3MP over SPI |
| ToF confused by sunlight outdoors | False alarms | Combine with camera detections; reduce range outdoors |
| Battery life too short | R5 missed | Duty-cycle the camera, run AI only on button press or when ToF sees something |
| Component delays | Schedule | Order early; keep breakout-board prototype working |

## Team roles (fill in)

| Area | Owner |
|---|---|
| Project lead, firmware architecture | Md. Mehedi Hassan |
| Camera + image pipeline | |
| ML dataset and training | |
| Hardware / PCB | |
| User research and testing | |
