// Vision AI Assist — main application.
//
// Tasks (highest priority first):
//   tofTask    4  every 33 ms   read 3 x VL53L0X, vibrate on danger, wake the AI task
//   audioTask  3  event         play queued voice prompts over I2S
//   aiTask     2  event         capture frame, run the classifier on L/C/R crops, fuse with ToF, speak
//   uiTask     1  every 20 ms   buttons, LCD status, LED, battery, serial commands
//
// Obstacle warnings never wait for the neural network: tofTask pre-empts aiTask.
#include "vision_app.h"
#include <STM32FreeRTOS.h>
#include <Wire.h>
#include "config.h"
#include "ai/detector.h"
#include "ai/labels.h"
#include "camera/dcmi_capture.h"
#include "camera/ov3660.h"
#include "feedback/audio_player.h"
#include "feedback/haptics.h"
#include "sensors/i2c_bus.h"
#include "sensors/tof_array.h"
#include "storage/sd_store.h"
#include "ui/display.h"

namespace {
TwoWire camWire(CAM_SCCB_SDA, CAM_SCCB_SCL);
OV3660 camera(camWire, CAM_SCCB_ADDR);
bool cameraOk = false, aiOk = false;

TaskHandle_t tofTaskH, aiTaskH, audioTaskH, uiTaskH;
SemaphoreHandle_t displayMutex;

volatile bool describeRequested = false;
volatile bool photoRequested = false;
volatile bool dumpFrameRequested = false;
volatile bool saveFrameRequested = false;   // dataset collection

char lastDetection[28] = "starting";
volatile uint32_t lastInferenceMs = 0;

const char *const kSideWords[SIDE_COUNT] = {"left", "ahead", "right"};

struct DisplayLock {
  DisplayLock() { xSemaphoreTake(displayMutex, portMAX_DELAY); }
  ~DisplayLock() { xSemaphoreGive(displayMutex); }
};

const char *distanceWord(uint16_t mm) {
  if (mm == TofArray::kNoTarget) return "far";
  if (mm < 700) return "close";
  if (mm < 1500) return "one_metre";
  return "two_metres";
}

// ---------------------------------------------------------------- camera modes
bool cameraAiMode() {
  return camera.setFormat(OV3660::Format::Gray) &&
         camera.setFrameSize(CAM_AI_WIDTH, CAM_AI_HEIGHT);
}

void takePhoto() {
  if (!cameraOk || !SdStore::ready()) {
    AudioPlayer::say("no_card");
    return;
  }
  AudioPlayer::beep(1500, 60);
  camera.setFormat(OV3660::Format::Jpeg);
  camera.setFrameSize(CAM_PHOTO_WIDTH, CAM_PHOTO_HEIGHT);
  camera.setQuality(CAM_JPEG_QUALITY);
  Dcmi::setJpegMode(true);
  vTaskDelay(pdMS_TO_TICKS(400));  // let exposure settle after the mode change

  uint8_t *buf = Dcmi::jpegBuffer();
  memset(buf, 0, CAM_JPEG_BUF_SIZE);
  bool ok = Dcmi::captureFrame(buf, CAM_JPEG_BUF_SIZE, 4000);
  uint32_t len = ok ? Dcmi::jpegLength(buf, CAM_JPEG_BUF_SIZE) : 0;
  char name[32] = "";
  if (len && SdStore::savePhoto(buf, len, name, sizeof(name))) {
    Serial.printf("[photo] saved %s (%lu bytes)\n", name, (unsigned long)len);
    AudioPlayer::say("photo_saved");
  } else {
    Serial.printf("[photo] failed (capture=%d, len=%lu) - try a higher CAM_JPEG_QUALITY\n", ok, (unsigned long)len);
    AudioPlayer::say("photo_failed");
  }
  Dcmi::setJpegMode(false);
  cameraAiMode();
  vTaskDelay(pdMS_TO_TICKS(200));
}

void dumpFrame(const uint8_t *gray) {
  // Read by tools/frame_viewer.py
  Serial.printf("FRAME %d %d\n", CAM_AI_WIDTH, CAM_AI_HEIGHT);
  Serial.write(gray, CAM_AI_WIDTH * CAM_AI_HEIGHT);
  Serial.println();
}

// ---------------------------------------------------------------- fusion
void announce(const Detection &d, bool describeAll) {
  static uint32_t lastSaid[SIDE_COUNT] = {0, 0, 0};
  static int lastClass[SIDE_COUNT] = {-1, -1, -1};
  const uint32_t now = millis();

  // Collect candidates (side, class, distance), nearest first.
  int order[SIDE_COUNT] = {SIDE_CENTRE, SIDE_LEFT, SIDE_RIGHT};
  uint16_t dist[SIDE_COUNT];
  for (int s = 0; s < SIDE_COUNT; s++) dist[s] = TofArray::distanceMm(s);
  for (int i = 0; i < SIDE_COUNT; i++)
    for (int j = i + 1; j < SIDE_COUNT; j++)
      if (dist[order[j]] < dist[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }

  int spoken = 0;
  lastDetection[0] = 0;
  for (int k = 0; k < SIDE_COUNT; k++) {
    const int s = order[k];
    const SideResult &r = d.side[s];
    const bool found = r.classIndex != AI_BACKGROUND_INDEX && r.score >= AI_SCORE_THRESHOLD;
    if (!found) continue;
    const char *label = Detector::label(r.classIndex);
    size_t n = strlen(lastDetection);
    snprintf(lastDetection + n, sizeof(lastDetection) - n, "%s%c%s %d%%", n ? " " : "",
             "LCR"[s], label, (int)(r.score * 100));
    const bool repeat = lastClass[s] == r.classIndex && now - lastSaid[s] < REPEAT_HOLDOFF_MS;
    if (repeat && !describeAll) continue;
    if (spoken >= (describeAll ? 3 : 1)) continue;
    const char *words[3] = {label, kSideWords[s], distanceWord(dist[s])};
    AudioPlayer::say(words, 3);
    lastSaid[s] = now;
    lastClass[s] = r.classIndex;
    spoken++;
  }
  if (!lastDetection[0]) strncpy(lastDetection, "clear", sizeof(lastDetection));
  if (describeAll && spoken == 0) AudioPlayer::say("nothing");
}

// ---------------------------------------------------------------- tasks
void tofTask(void *) {
  TickType_t wake = xTaskGetTickCount();
  uint32_t lastBuzz = 0, lastAiKick = 0;
  for (;;) {
    TofArray::update();
    const uint16_t c = TofArray::distanceMm(SIDE_CENTRE);
    const uint16_t l = TofArray::distanceMm(SIDE_LEFT);
    const uint16_t r = TofArray::distanceMm(SIDE_RIGHT);
    const uint32_t now = millis();

    // Pulse faster as the obstacle gets closer: 150 ms at 0 m ... 700 ms at the danger limit.
    if (c < DANGER_CENTRE_MM) {
      uint32_t interval = 150 + (uint32_t)c * 550 / DANGER_CENTRE_MM;
      if (now - lastBuzz >= interval) { Haptics::danger(); lastBuzz = now; }
    } else if (l < DANGER_SIDE_MM || r < DANGER_SIDE_MM) {
      if (now - lastBuzz >= 600) { Haptics::warning(); lastBuzz = now; }
    }

    const bool near = c < AI_TRIGGER_MM || l < AI_TRIGGER_MM || r < AI_TRIGGER_MM;
    if (near && now - lastAiKick > 500) { xTaskNotifyGive(aiTaskH); lastAiKick = now; }
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(TOF_PERIOD_MS));
  }
}

void aiTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(AI_IDLE_PERIOD_MS));
    if (photoRequested) { photoRequested = false; takePhoto(); continue; }
    if (!cameraOk) continue;

    const bool describe = describeRequested;
    describeRequested = false;
    uint8_t *frame = Dcmi::aiFrame();
    if (!Dcmi::captureFrame(frame, CAM_AI_WIDTH * CAM_AI_HEIGHT, 1000)) {
      Serial.println("[cam] frame timeout - check DCMI polarity in config.h");
      continue;
    }
    if (dumpFrameRequested) { dumpFrameRequested = false; dumpFrame(frame); }
    if (saveFrameRequested) {
      saveFrameRequested = false;
      char name[32];
      if (SdStore::savePgm(frame, CAM_AI_WIDTH, CAM_AI_HEIGHT, name, sizeof(name))) {
        Serial.printf("[dataset] saved %s\n", name);
        AudioPlayer::beep(2000, 40);
      }
    }
    { DisplayLock lock; Display::preview(frame, CAM_AI_WIDTH, CAM_AI_HEIGHT); }

    if (!aiOk) continue;
    Detection det;
    if (!Detector::run(frame, CAM_AI_WIDTH, CAM_AI_HEIGHT, det)) continue;
    lastInferenceMs = det.inferenceMs;
    announce(det, describe);
    Serial.printf("[ai] %lu ms  L:%s %.2f  C:%s %.2f  R:%s %.2f\n", (unsigned long)det.inferenceMs,
                  Detector::label(det.side[0].classIndex), det.side[0].score,
                  Detector::label(det.side[1].classIndex), det.side[1].score,
                  Detector::label(det.side[2].classIndex), det.side[2].score);
  }
}

void audioTask(void *) { AudioPlayer::taskLoop(); }

struct Button {
  uint8_t pin;
  bool activeHigh;
  bool down = false;
  uint32_t since = 0;
  bool longFired = false;
};

void uiTask(void *) {
  Button k1{BTN_DESCRIBE, true}, mute{BTN_MUTE, false}, vol{BTN_VOLUME, false};
  uint32_t lastScreen = 0, lastBattery = 0;
  bool batteryWarned = false;
  for (;;) {
    const uint32_t now = millis();
    for (Button *b : {&k1, &mute, &vol}) {
      bool pressed = digitalRead(b->pin) == (b->activeHigh ? HIGH : LOW);
      if (pressed && !b->down) { b->down = true; b->since = now; b->longFired = false; }
      if (pressed && b->down && !b->longFired && now - b->since > LONG_PRESS_MS && b == &k1) {
        b->longFired = true;
        photoRequested = true;
        Haptics::ack();
        xTaskNotifyGive(aiTaskH);
      }
      if (!pressed && b->down) {
        b->down = false;
        if (now - b->since < 30 || b->longFired) continue;  // bounce or long press already handled
        Haptics::ack();
        if (b == &k1) { describeRequested = true; xTaskNotifyGive(aiTaskH); }
        if (b == &mute) { AudioPlayer::setMuted(!AudioPlayer::muted()); if (!AudioPlayer::muted()) AudioPlayer::say("sound_on"); }
        if (b == &vol) { AudioPlayer::cycleVolume(); AudioPlayer::beep(1000, 80); }
      }
    }

    // Serial commands: 'f' = send frame to PC, 's' = save frame to SD (dataset), 'p' = photo, 'd' = describe
    while (Serial.available()) {
      char c = Serial.read();
      if (c == 'f') { dumpFrameRequested = true; xTaskNotifyGive(aiTaskH); }
      if (c == 'p') { photoRequested = true; xTaskNotifyGive(aiTaskH); }
      if (c == 'd') { describeRequested = true; xTaskNotifyGive(aiTaskH); }
      if (c == 's') { saveFrameRequested = true; xTaskNotifyGive(aiTaskH); }
    }

    if (now - lastScreen > 200) {
      lastScreen = now;
      digitalWrite(LED_STATUS, (now / 500) & 1);
      DisplayLock lock;
      Display::status(TofArray::distanceMm(SIDE_LEFT), TofArray::distanceMm(SIDE_CENTRE),
                      TofArray::distanceMm(SIDE_RIGHT), lastDetection, lastInferenceMs, AudioPlayer::muted());
    }
    if (now - lastBattery > 10000) {
      lastBattery = now;
      float v = analogRead(BATTERY_ADC) * 3.3f * 2.0f / 4095.0f;
      if (v > 2.5f && v < 3.4f && !batteryWarned) { AudioPlayer::say("battery_low"); batteryWarned = true; }
      if (v > 3.6f) batteryWarned = false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
}  // namespace

void VisionApp::start() {
  SCB_EnableICache();  // instruction cache is always safe; see README before enabling the D-cache
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) {}
  Serial.println("\n=== Vision AI Assist (STM32H743) ===");

  pinMode(LED_STATUS, OUTPUT);
  pinMode(BTN_DESCRIBE, INPUT_PULLDOWN);
  pinMode(BTN_MUTE, INPUT_PULLUP);
  pinMode(BTN_VOLUME, INPUT_PULLUP);
  analogReadResolution(12);
  displayMutex = xSemaphoreCreateMutex();

  Display::begin();
  Display::message("Vision AI Assist", "starting...");

  I2CBus::begin();
  bool tofOk = TofArray::begin();
  Haptics::begin();
  SdStore::begin();
  AudioPlayer::begin();

  cameraOk = Dcmi::begin();
  Dcmi::xclkOn();
  delay(50);
  cameraOk = cameraOk && camera.begin() && cameraAiMode();
  aiOk = Detector::begin();

  char line[32];
  snprintf(line, sizeof(line), "cam:%s ai:%s tof:%s", cameraOk ? "ok" : "X", aiOk ? "ok" : "X", tofOk ? "ok" : "X");
  Serial.println(line);
  Display::message("Vision AI Assist", line);

  xTaskCreate(tofTask, "tof", 1024, nullptr, 4, &tofTaskH);
  xTaskCreate(audioTask, "audio", 1536, nullptr, 3, &audioTaskH);
  xTaskCreate(aiTask, "ai", 4096, nullptr, 2, &aiTaskH);
  xTaskCreate(uiTask, "ui", 1536, nullptr, 1, &uiTaskH);
  AudioPlayer::say("ready");
  vTaskStartScheduler();
  for (;;) {}  // only reached if the scheduler could not start
}
