# Software Design

## Core split

| Core | Runs | Why |
|---|---|---|
| **Cortex-M7** (480 MHz, FPU, caches) | Camera capture, pre-processing, neural network | Heavy maths, large memory |
| **Cortex-M4** (240 MHz) on **FreeRTOS** | ToF, buttons, fusion, audio, haptics, power | Fast, predictable reaction to obstacles even while the M7 is busy |

The M4 can warn about obstacles **without** the M7, so safety never waits for the neural network.

## M7 image pipeline

```
OV3660 ──DCMI+DMA──► SDRAM frame buffer (VGA YUV422)
          │
          ▼  DMA2D: crop + resize + colour convert
   192×192 RGB888 input tensor (AXI SRAM)
          │
          ▼  STM32Cube.AI INT8 network
   boxes + classes + scores
          │
          ▼  NMS, score threshold
   detections ──► shared SRAM + HSEM notify ──► M4
```

- Full 3 MP JPEG snapshots are captured only on request (for future text reading).
- Use D-cache with care: clean/invalidate around DMA buffers or place them in non-cacheable MPU regions.

## Model

| Step | Tool |
|---|---|
| Start point | ST Model Zoo object-detection models (`st_ssd_mobilenet_v1`, `st_yolo_lc_v1`) |
| Data | COCO subset for our classes + local images labelled in Label Studio / Roboflow |
| Train | TensorFlow / Keras (Model Zoo scripts) |
| Quantise | Post-training INT8 (TFLite converter), check mAP drop < 3 % |
| Deploy | STM32Cube.AI → C code, `ai_network_run()` |
| Validate | On-target FPS, RAM, flash, accuracy on held-out set |

Budget on the M7: activations ≤ 512 KB (AXI SRAM), weights ≤ 1.5 MB (internal flash; QSPI if larger).

## M4 tasks (FreeRTOS)

| Task | Priority | Period | Job |
|---|---|---|---|
| `tofTask` | High | 66 ms | Read VL53L5CX 8×8, filter, nearest distance per sector |
| `fusionTask` | Medium | event | Combine detections with ToF zones → distance + direction |
| `feedbackTask` | Medium | event | Pick message, play voice prompt, drive haptics |
| `uiTask` | Low | event | Buttons, mute, volume, battery |
| `powerTask` | Low | 1 s | Battery level, sleep when idle |

## Feedback rules

1. **Danger first:** anything under 1 m in the centre → strong fast vibration immediately (M4 only, no AI needed).
2. **Moving or important objects:** person, car, motorbike, bicycle, dog → voice prompt with distance and side.
3. **Navigation objects:** door, stairs → voice prompt.
4. **Do not repeat** the same message within 5 s; drop old messages.
5. **Describe button:** speak up to 3 objects, nearest first.

## Folder plan

```
firmware/
  CM7/   Core/, Drivers/, Middlewares/ (X-CUBE-AI), App/camera, App/preproc, App/ai
  CM4/   Core/, Drivers/, Middlewares/FreeRTOS, App/tof, App/fusion, App/audio, App/haptics
  Common/  shared message structs, HSEM ids
ml/
  data/ (not committed), configs/, train.py, quantize.py, eval.py, export_cubeai.md
tools/
  frame_viewer.py   USB-CDC frame + box viewer
  make_prompts.py   builds the voice-clip table for QSPI
```

## Coding rules

- C11, HAL + LL where speed matters, no dynamic memory after start-up
- Each driver has a small test in `tests/`
- Measure, then optimise: log FPS, latency and current for every milestone
