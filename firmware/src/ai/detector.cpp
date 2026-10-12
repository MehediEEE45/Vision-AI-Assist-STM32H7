#include "detector.h"
#include "config.h"
#include <math.h>
#include <Chirale_TensorFlowLite.h>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "labels.h"
#include "model_data.h"

namespace {
// person_detect needs ~136 KB; custom MobileNet models from ml/ need up to ~200 KB.
constexpr size_t kArenaSize = 200 * 1024;
alignas(16) uint8_t tensorArena[kArenaSize];

const tflite::Model *model = nullptr;
tflite::MicroInterpreter *interpreter = nullptr;
TfLiteTensor *input = nullptr;
TfLiteTensor *output = nullptr;
int inSize = 96;
int inChannels = 1;

// Ops used by person_detect and by the MobileNet models produced in ml/.
using OpResolver = tflite::MicroMutableOpResolver<10>;
OpResolver &resolver() {
  static OpResolver r;
  static bool done = false;
  if (!done) {
    r.AddAveragePool2D();
    r.AddConv2D();
    r.AddDepthwiseConv2D();
    r.AddReshape();
    r.AddSoftmax();
    r.AddFullyConnected();
    r.AddMean();
    r.AddQuantize();
    r.AddDequantize();
    r.AddPad();
    done = true;
  }
  return r;
}

// Bilinear resize of a square region of the grayscale frame into the int8 input tensor.
void fillInput(const uint8_t *gray, int w, int x0, int y0, int side) {
  const float scale = (float)side / inSize;
  const bool isInt8 = input->type == kTfLiteInt8;
  // Quantise with the model's own input parameters. All supported models take inputs
  // normalised to [-1, 1] (person_detect: scale 1/127.5, zero point -1 -> q = pixel - 128).
  const float invScale = input->params.scale > 0 ? 1.0f / input->params.scale : 1.0f;
  const int zeroPoint = input->params.zero_point;
  int8_t *dst8 = input->data.int8;
  uint8_t *dstU = input->data.uint8;
  for (int y = 0; y < inSize; y++) {
    float fy = (y + 0.5f) * scale - 0.5f;
    int iy = (int)fy; float dy = fy - iy;
    if (iy < 0) { iy = 0; dy = 0; }
    if (iy >= side - 1) { iy = side - 2; dy = 1; }
    const uint8_t *r0 = gray + (y0 + iy) * w + x0;
    const uint8_t *r1 = r0 + w;
    for (int x = 0; x < inSize; x++) {
      float fx = (x + 0.5f) * scale - 0.5f;
      int ix = (int)fx; float dx = fx - ix;
      if (ix < 0) { ix = 0; dx = 0; }
      if (ix >= side - 1) { ix = side - 2; dx = 1; }
      float top = r0[ix] + (r0[ix + 1] - r0[ix]) * dx;
      float bot = r1[ix] + (r1[ix + 1] - r1[ix]) * dx;
      float v = top + (bot - top) * dy;          // pixel 0..255
      float xn = v * (1.0f / 127.5f) - 1.0f;      // MobileNet convention: [-1, 1]
      int q = (int)floorf(xn * invScale + 0.5f) + zeroPoint;
      if (isInt8) { if (q < -128) q = -128; if (q > 127) q = 127; }
      else        { if (q < 0) q = 0;       if (q > 255) q = 255; }
      for (int c = 0; c < inChannels; c++) {
        int idx = (y * inSize + x) * inChannels + c;
        if (isInt8) dst8[idx] = (int8_t)q;
        else dstU[idx] = (uint8_t)q;
      }
    }
  }
}

float outputProb(int i) {
  if (output->type == kTfLiteInt8) {
    return (output->data.int8[i] - output->params.zero_point) * output->params.scale;
  }
  if (output->type == kTfLiteUInt8) {
    return (output->data.uint8[i] - output->params.zero_point) * output->params.scale;
  }
  return output->data.f[i];
}
}  // namespace

bool Detector::begin() {
  model = tflite::GetModel(g_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.printf("[ai] model schema %lu != %d\n", (unsigned long)model->version(), TFLITE_SCHEMA_VERSION);
    return false;
  }
  static tflite::MicroInterpreter interp(model, resolver(), tensorArena, kArenaSize);
  interpreter = &interp;
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println("[ai] AllocateTensors failed (arena too small or unsupported op)");
    return false;
  }
  input = interpreter->input(0);
  output = interpreter->output(0);
  inSize = input->dims->data[1];
  inChannels = input->dims->data[3];
  int outClasses = output->dims->data[output->dims->size - 1];
  Serial.printf("[ai] model ready: input %dx%dx%d, %d classes, arena %u bytes\n", inSize, inSize,
                inChannels, outClasses, (unsigned)interpreter->arena_used_bytes());
  if (outClasses != kNumLabels) {
    Serial.printf("[ai] WARNING: model has %d outputs but labels.h has %d names\n", outClasses, kNumLabels);
  }
  return true;
}

bool Detector::run(const uint8_t *gray, int w, int h, Detection &out) {
  if (!interpreter) return false;
  // Three squares: left, centre, right, taken from the middle rows of the frame.
  int side = AI_CROP_SIZE;
  if (side > w) side = w;
  if (side > h) side = h;
  const int xs[SIDE_COUNT] = {0, (w - side) / 2, w - side};
  const int y0 = (h - side) / 2;
  const int outClasses = output->dims->data[output->dims->size - 1];
  uint32_t t0 = millis();
  for (int s = 0; s < SIDE_COUNT; s++) {
    fillInput(gray, w, xs[s], y0, side);
    if (interpreter->Invoke() != kTfLiteOk) return false;
    int best = 0;
    float bestP = outputProb(0);
    for (int i = 1; i < outClasses; i++) {
      float p = outputProb(i);
      if (p > bestP) { bestP = p; best = i; }
    }
    out.side[s].classIndex = best;
    out.side[s].score = bestP;
  }
  out.inferenceMs = millis() - t0;
  return true;
}

int Detector::inputSize() { return inSize; }
size_t Detector::arenaUsed() { return interpreter ? interpreter->arena_used_bytes() : 0; }
const char *Detector::label(int i) { return (i >= 0 && i < kNumLabels) ? kLabels[i] : "object"; }
