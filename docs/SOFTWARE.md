# Software Design

Target: **WeAct STM32H743VIT6** — one Cortex-M7, 1 MB RAM, no SDRAM. See [HARDWARE.md](HARDWARE.md).

## Architecture

Everything runs on the single M7 under **FreeRTOS**. Priorities guarantee that obstacle warnings are never blocked by the neural network: the AI task runs at the lowest active priority and is pre-empted whenever the ToF task has new data.

| Task | Priority | Period | Job |
|---|---|---|---|
| `tofTask` | Highest | 33 ms (30 Hz) | Read the 3 VL53L0X sensors (left / centre / right), median filter, flag invalid readings |
| `alertTask` | High | event | Danger rule (< 1 m centre) → haptics immediately |
| `cameraTask` | Medium | event | DCMI frame done → hand buffer to AI, or save JPEG snapshot to SD |
| `fusionTask` | Medium | event | Match each detection box to the ToF sensor covering its horizontal position → distance + side |
| `audioTask` | Medium | event | Play voice prompt from QSPI over I2S DMA |
| `aiTask` | Low | continuous / on demand | Pre-process + STM32Cube.AI inference |
| `uiTask` | Low | event | Buttons, LCD status, battery |
| `logTask` | Lowest | 1 s | FPS, latency, battery to SD / USB |

## Camera pipeline

```
OV3660 ──DCMI + DMA (snapshot or continuous)──► frame buffer in SRAM1-3
          (240×240 RGB565 window, or QVGA YUV422)
                         │
                         ▼  DMA2D: colour convert, scale to model size
              192×192 (or 160×160) RGB888 tensor in AXI SRAM
                         │
                         ▼  STM32Cube.AI INT8 network (weights in flash)
              boxes + classes + scores → NMS → detections → fusionTask

On K1 long-press: switch OV3660 to JPEG 2048×1536 → DCMI DMA in chunks → microSD (FatFs)
```

- No SDRAM, so the AI frame never exceeds ~150 KB. Use the OV3660 windowing/scaler to output 240×240 directly instead of scaling large frames in software.
- Full 3 MP images are only taken as JPEG (typically 150–400 KB) and streamed to the SD card in chunks.
- DMA buffers live in SRAM1–3 in a non-cacheable MPU region.

## Model

**Implemented in firmware v0.1:** a classifier (not a box detector) runs on three 160×160 squares — left, centre and right — taken from the middle of a 320×240 grayscale frame. Each square maps to one VL53L0X, which gives the distance. It ships with Google's pretrained person-detection model and can be swapped for a custom multi-class model trained with `firmware/ml/train_classifier.py`. A full object detector (boxes) remains the Phase 3 goal below.


| Step | Tool |
|---|---|
| Start point | ST Model Zoo object detection (`st_ssd_mobilenet_v1`, `st_yolo_lc_v1`) — chosen for MCUs without an NPU |
| Classes | person, door, stairs, chair, table, car, motorbike, bicycle, pole, dog |
| Data | COCO subset + local photos from SUST campus and Sylhet streets, labelled in Label Studio / Roboflow |
| Train | TensorFlow / Keras with Model Zoo scripts |
| Quantise | INT8 post-training quantisation, accuracy drop < 3 % mAP |
| Deploy | STM32Cube.AI (X-CUBE-AI) → C code; check "Analyze" report for flash/RAM |
| Validate | On-target FPS, latency, accuracy on held-out set |

**Budget:** activations ≤ 400 KB (AXI SRAM), weights ≤ 1.5 MB (internal flash; memory-mapped QSPI if larger, at some speed cost). Input 192×192 first; drop to 160×160 or fewer classes if FPS < 5.

## ToF fusion

The camera image is split into three vertical bands that match the three sensors' fields of view:

| Box centre in image | Distance from | Spoken side |
|---|---|---|
| left third | left VL53L0X | "left" |
| middle third | centre VL53L0X | "ahead" |
| right third | right VL53L0X | "right" |

If the matching sensor reads beyond 2 m or is invalid, the object is announced without distance ("person, far, left"). The exact band edges are calibrated in Phase 4 by placing an object at known angles.

## Feedback rules

1. **Danger first:** anything closer than 1 m on the centre sensor (or 0.6 m on a side sensor) → strong, fast vibration straight from `alertTask` (no AI needed).
2. **Important objects:** person, car, motorbike, bicycle, dog → voice prompt with distance and side (*"car, three metres, right"*).
3. **Navigation objects:** door, stairs → voice prompt.
4. Do not repeat the same message within 5 s; drop stale messages.
5. **K1 short press:** describe up to 3 objects, nearest first. **Long press:** save a 3 MP photo to SD.

## Power strategy

- Camera + AI run only when ToF sees something within 3 m, or after K1 is pressed.
- CPU at 480 MHz during inference, 240 MHz otherwise, Stop mode when idle with ToF interrupt as wake-up.
- LCD off in wearable mode.

## Folder plan

```
firmware/
  Core/ Drivers/ Middlewares/ (FreeRTOS, FatFs, X-CUBE-AI)
  App/camera   OV3660 driver, DCMI capture, JPEG snapshot
  App/ai       pre-processing, network wrapper, NMS
  App/tof      VL53L0X driver (ST API), 3-sensor address setup, filtering
  App/fusion   box → zone mapping, priority rules
  App/audio    I2S player, prompt table in QSPI
  App/haptics  DRV2605L
  App/ui       buttons, ST7735 status screen
ml/
  configs/  train.py  quantize.py  eval.py  export_cubeai.md
tools/
  frame_viewer.py   USB frame + detection viewer
  make_prompts.py   builds the voice-prompt image for QSPI
```

## Getting started (Phase 1)

1. Clone WeAct examples `08-DCMI2LCD` (camera → LCD) and `05-DCMI_UVC` (camera → PC as USB webcam) from [WeActStudio/MiniSTM32H7xx](https://github.com/WeActStudio/MiniSTM32H7xx/tree/master/SDK/HAL/STM32H743).
2. Run them with an OV2640/OV5640 first to prove the board, then add the OV3660 register table.
3. Raise the system clock from 240 MHz to 480 MHz (VOS0) and confirm stability.
