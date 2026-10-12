#pragma once
// Class names for the model in model_data.cpp, in output order.
// Default model: TensorFlow "person_detect" (Visual Wake Words, 96x96 grayscale).
// After training your own model with ml/train_classifier.py, replace this list with
// the printed class order and regenerate model_data.cpp with tools/tflite_to_c.py.
#define AI_BACKGROUND_INDEX 0   // class that means "nothing to announce"

static const char *const kLabels[] = {
    "nothing",  // 0  (no person)
    "person",   // 1
};
static const int kNumLabels = sizeof(kLabels) / sizeof(kLabels[0]);
