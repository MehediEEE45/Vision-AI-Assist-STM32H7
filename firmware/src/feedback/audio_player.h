#pragma once
#include <Arduino.h>

// Voice prompts through I2S2 -> MAX98357A. Prompts are 16 kHz 16-bit mono WAV files
// in /prompts on the microSD card (make them with tools/make_prompts.py).
// If a file is missing, a short beep pattern is played instead.
namespace AudioPlayer {
bool begin();
void taskLoop();                         // run inside the audio FreeRTOS task (never returns)

// Queue a sentence made of up to 4 prompt names, e.g. {"person", "left", "close"}.
bool say(const char *const *words, uint8_t count);
bool say(const char *word);
void beep(uint16_t freqHz, uint16_t ms);  // queue a tone
void setMuted(bool muted);
bool muted();
uint8_t cycleVolume();                    // 1..4, returns new level
}  // namespace AudioPlayer
