#include "audio_player.h"
#include <STM32FreeRTOS.h>
#include <math.h>
#include "config.h"
#include "storage/sd_store.h"

namespace {
I2S_HandleTypeDef hi2s;
DMA_HandleTypeDef hdmaTx;
TaskHandle_t audioTask = nullptr;

constexpr int kFrames = 512;                  // stereo frames per half buffer (32 ms)
alignas(32) int16_t dmaBuf[2 * kFrames * 2];  // two halves x frames x (L,R)
volatile int8_t halfToFill = -1;              // set by DMA ISR

struct Message {
  char words[4][16];
  uint8_t count;
  uint16_t toneHz, toneMs;                    // used when count == 0
};
QueueHandle_t queue = nullptr;
volatile bool isMuted = false;
volatile uint8_t volume = 3;                  // 1..4

void gpioI2S() {
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitTypeDef g = {};
  g.Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15;  // WS, CK, SDO
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(GPIOB, &g);
}

// --- streaming sources -----------------------------------------------------
File wav;
uint32_t wavRemaining = 0;
uint16_t wavChannels = 1;
uint32_t toneSamplesLeft = 0, tonePhase = 0, toneStep = 0;

bool openWav(const char *name) {
  if (!SdStore::ready()) return false;
  char path[40];
  snprintf(path, sizeof(path), "/prompts/%s.wav", name);
  SdStore::Guard g;
  wav = SD.open(path, FILE_READ);
  if (!wav) return false;
  uint8_t h[12];
  if (wav.read(h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { wav.close(); return false; }
  // Walk the chunks until "data"; read channel count from "fmt ".
  while (wav.available() >= 8) {
    uint8_t ch[8];
    wav.read(ch, 8);
    uint32_t sz = ch[4] | (ch[5] << 8) | (ch[6] << 16) | ((uint32_t)ch[7] << 24);
    if (!memcmp(ch, "fmt ", 4)) {
      uint8_t fmt[16];
      wav.read(fmt, 16);
      wavChannels = fmt[2] | (fmt[3] << 8);
      if (sz > 16) wav.seek(wav.position() + (sz - 16));
    } else if (!memcmp(ch, "data", 4)) {
      wavRemaining = sz;
      return true;
    } else {
      wav.seek(wav.position() + sz + (sz & 1));
    }
  }
  wav.close();
  return false;
}

// Fill one half buffer. Returns false when the current source is finished.
bool fillHalf(int16_t *dst) {
  const int gainQ8 = isMuted ? 0 : (64 * volume);  // 64..256 = 25..100 %
  int frames = 0;
  if (wavRemaining) {
    static int16_t tmp[kFrames * 2];
    uint32_t want = kFrames * 2 * wavChannels;
    if (want > wavRemaining) want = wavRemaining;
    int got;
    { SdStore::Guard g; got = wav.read((uint8_t *)tmp, want); }
    if (got <= 0) { wavRemaining = 0; }
    else {
      wavRemaining -= got;
      frames = got / (2 * wavChannels);
      for (int i = 0; i < frames; i++) {
        int32_t s = (wavChannels == 2) ? (tmp[2 * i] + tmp[2 * i + 1]) / 2 : tmp[i];
        s = (s * gainQ8) >> 8;
        dst[2 * i] = dst[2 * i + 1] = (int16_t)s;
      }
    }
    if (!wavRemaining) { SdStore::Guard g; wav.close(); }
  } else if (toneSamplesLeft) {
    frames = toneSamplesLeft < (uint32_t)kFrames ? toneSamplesLeft : kFrames;
    for (int i = 0; i < frames; i++) {
      int32_t s = (int32_t)(sinf(tonePhase * (2.0f * PI / 65536.0f)) * 9000.0f);
      tonePhase = (tonePhase + toneStep) & 0xFFFF;
      s = (s * gainQ8) >> 8;
      dst[2 * i] = dst[2 * i + 1] = (int16_t)s;
    }
    toneSamplesLeft -= frames;
  }
  for (int i = frames; i < kFrames; i++) dst[2 * i] = dst[2 * i + 1] = 0;  // pad with silence
  return frames > 0;
}

void startTone(uint16_t hz, uint16_t ms) {
  toneSamplesLeft = (uint32_t)AUDIO_SAMPLE_RATE * ms / 1000;
  toneStep = (uint32_t)hz * 65536u / AUDIO_SAMPLE_RATE;
  tonePhase = 0;
}
}  // namespace

bool AudioPlayer::begin() {
  queue = xQueueCreate(6, sizeof(Message));
  // I2S kernel clock: PLL1Q = 48 MHz on this board's clock tree.
  RCC_PeriphCLKInitTypeDef pclk = {};
  pclk.PeriphClockSelection = RCC_PERIPHCLK_SPI123;
  pclk.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL;
  HAL_RCCEx_PeriphCLKConfig(&pclk);
  __HAL_RCC_SPI2_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();
  gpioI2S();

  hdmaTx.Instance = DMA1_Stream1;
  hdmaTx.Init.Request = DMA_REQUEST_SPI2_TX;
  hdmaTx.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdmaTx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdmaTx.Init.MemInc = DMA_MINC_ENABLE;
  hdmaTx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdmaTx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  hdmaTx.Init.Mode = DMA_CIRCULAR;
  hdmaTx.Init.Priority = DMA_PRIORITY_MEDIUM;
  hdmaTx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&hdmaTx) != HAL_OK) return false;

  hi2s.Instance = SPI2;
  hi2s.Init.Mode = I2S_MODE_MASTER_TX;
  hi2s.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s.Init.DataFormat = I2S_DATAFORMAT_16B;
  hi2s.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
  hi2s.Init.AudioFreq = I2S_AUDIOFREQ_16K;
  hi2s.Init.CPOL = I2S_CPOL_LOW;
  hi2s.Init.FirstBit = I2S_FIRSTBIT_MSB;
  hi2s.Init.WSInversion = I2S_WS_INVERSION_DISABLE;
  hi2s.Init.Data24BitAlignment = I2S_DATA_24BIT_ALIGNMENT_RIGHT;
  hi2s.Init.MasterKeepIOState = I2S_MASTER_KEEP_IO_STATE_ENABLE;
  if (HAL_I2S_Init(&hi2s) != HAL_OK) return false;
  __HAL_LINKDMA(&hi2s, hdmatx, hdmaTx);

  HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 7, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
  Serial.println("[audio] I2S ready");
  return true;
}

bool AudioPlayer::say(const char *const *words, uint8_t count) {
  if (!queue || isMuted) return false;
  Message m = {};
  m.count = count > 4 ? 4 : count;
  for (int i = 0; i < m.count; i++) strncpy(m.words[i], words[i], sizeof(m.words[i]) - 1);
  return xQueueSend(queue, &m, 0) == pdTRUE;
}

bool AudioPlayer::say(const char *word) { return say(&word, 1); }

void AudioPlayer::beep(uint16_t hz, uint16_t ms) {
  if (!queue || isMuted) return;
  Message m = {};
  m.toneHz = hz;
  m.toneMs = ms;
  xQueueSend(queue, &m, 0);
}

void AudioPlayer::setMuted(bool m) {
  isMuted = m;
  if (m && queue) xQueueReset(queue);
}
bool AudioPlayer::muted() { return isMuted; }
uint8_t AudioPlayer::cycleVolume() { volume = volume >= 4 ? 1 : volume + 1; return volume; }

void AudioPlayer::taskLoop() {
  audioTask = xTaskGetCurrentTaskHandle();
  Message m;
  for (;;) {
    if (xQueueReceive(queue, &m, portMAX_DELAY) != pdTRUE) continue;
    // Build the playlist: each word is a WAV; missing words become a beep.
    for (int w = 0; w < (m.count ? m.count : 1); w++) {
      if (m.count) {
        if (!openWav(m.words[w])) startTone(1200, 120);
      } else {
        startTone(m.toneHz, m.toneMs);
      }
      fillHalf(dmaBuf);
      fillHalf(dmaBuf + kFrames * 2);
      SCB_CleanDCache_by_Addr((uint32_t *)dmaBuf, sizeof(dmaBuf));
      if (HAL_I2S_Transmit_DMA(&hi2s, (uint16_t *)dmaBuf, kFrames * 2 * 2) != HAL_OK) break;
      bool more = true;
      int silentHalves = 0;
      while (silentHalves < 2) {  // let the last real half play out
        if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500))) break;
        int16_t *half = dmaBuf + (halfToFill ? kFrames * 2 : 0);
        more = more && fillHalf(half);
        if (!more) { memset(half, 0, kFrames * 4); silentHalves++; }
        SCB_CleanDCache_by_Addr((uint32_t *)half, kFrames * 4);
      }
      HAL_I2S_DMAStop(&hi2s);
      if (wavRemaining) { SdStore::Guard g; wav.close(); wavRemaining = 0; }
      toneSamplesLeft = 0;
      vTaskDelay(pdMS_TO_TICKS(40));  // short gap between words
    }
  }
}

static void notifyFromIsr(int8_t half) {
  halfToFill = half;
  if (!audioTask) return;
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(audioTask, &woken);
  portYIELD_FROM_ISR(woken);
}

extern "C" {
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *) { notifyFromIsr(0); }
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *) { notifyFromIsr(1); }
void DMA1_Stream1_IRQHandler(void) { HAL_DMA_IRQHandler(&hdmaTx); }
}
