#include "dcmi_capture.h"
#include <STM32FreeRTOS.h>
#include "config.h"

static DCMI_HandleTypeDef hdcmi;
static DMA_HandleTypeDef hdmaDcmi;
static volatile TaskHandle_t waitingTask = nullptr;
static volatile bool frameError = false;

// AI frame lives in normal RAM (AXI SRAM, reachable by DMA2). 32-byte aligned for cache ops.
alignas(32) static uint8_t aiFrameBuf[CAM_AI_WIDTH * CAM_AI_HEIGHT];
// The linker does not use D2 SRAM (0x30000000, 288 KB) on this variant: put the JPEG buffer there.
static uint8_t *const jpegBuf = reinterpret_cast<uint8_t *>(0x30000000);

uint8_t *Dcmi::aiFrame() { return aiFrameBuf; }
uint8_t *Dcmi::jpegBuffer() { return jpegBuf; }

void Dcmi::xclkOn() {
  // HSI48 / 4 = 12 MHz on MCO1 (PA8), same as WeAct's DCMI example.
  RCC_OscInitTypeDef osc = {};
  osc.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
  osc.HSI48State = RCC_HSI48_ON;
  osc.PLL.PLLState = RCC_PLL_NONE;
  HAL_RCC_OscConfig(&osc);
  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSI48, RCC_MCODIV_4);
}

static void gpioAf13(GPIO_TypeDef *port, uint32_t pins) {
  GPIO_InitTypeDef g = {};
  g.Pin = pins;
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF13_DCMI;
  HAL_GPIO_Init(port, &g);
}

static bool dcmiInit(bool jpeg) {
  hdcmi.Instance = DCMI;
  hdcmi.Init.SynchroMode = DCMI_SYNCHRO_HARDWARE;
  hdcmi.Init.PCKPolarity = DCMI_PCKPOLARITY_RISING;
  hdcmi.Init.VSPolarity = CAM_DCMI_VSPOLARITY;
  hdcmi.Init.HSPolarity = CAM_DCMI_HSPOLARITY;
  hdcmi.Init.CaptureRate = DCMI_CR_ALL_FRAME;
  hdcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
  hdcmi.Init.JPEGMode = jpeg ? DCMI_JPEG_ENABLE : DCMI_JPEG_DISABLE;
  hdcmi.Init.ByteSelectMode = DCMI_BSM_ALL;
  hdcmi.Init.ByteSelectStart = DCMI_OEBS_ODD;
  hdcmi.Init.LineSelectMode = DCMI_LSM_ALL;
  hdcmi.Init.LineSelectStart = DCMI_OELS_ODD;
  return HAL_DCMI_Init(&hdcmi) == HAL_OK;
}

bool Dcmi::begin() {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_DCMI_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_D2SRAM1_CLK_ENABLE();
  __HAL_RCC_D2SRAM2_CLK_ENABLE();
  __HAL_RCC_D2SRAM3_CLK_ENABLE();

  gpioAf13(GPIOA, GPIO_PIN_4 | GPIO_PIN_6);              // HSYNC, PIXCLK
  gpioAf13(GPIOB, GPIO_PIN_7);                           // VSYNC
  gpioAf13(GPIOC, GPIO_PIN_6 | GPIO_PIN_7);              // D0, D1
  gpioAf13(GPIOD, GPIO_PIN_3);                           // D5
  gpioAf13(GPIOE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6);  // D2 D3 D4 D6 D7

  hdmaDcmi.Instance = DMA2_Stream0;
  hdmaDcmi.Init.Request = DMA_REQUEST_DCMI;
  hdmaDcmi.Init.Direction = DMA_PERIPH_TO_MEMORY;
  hdmaDcmi.Init.PeriphInc = DMA_PINC_DISABLE;
  hdmaDcmi.Init.MemInc = DMA_MINC_ENABLE;
  hdmaDcmi.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  hdmaDcmi.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
  hdmaDcmi.Init.Mode = DMA_NORMAL;
  hdmaDcmi.Init.Priority = DMA_PRIORITY_HIGH;
  hdmaDcmi.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
  hdmaDcmi.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
  hdmaDcmi.Init.MemBurst = DMA_MBURST_INC4;
  hdmaDcmi.Init.PeriphBurst = DMA_PBURST_SINGLE;
  if (HAL_DMA_Init(&hdmaDcmi) != HAL_OK) return false;
  __HAL_LINKDMA(&hdcmi, DMA_Handle, hdmaDcmi);

  if (!dcmiInit(false)) return false;
  // Priorities must be numerically >= configMAX_SYSCALL_INTERRUPT_PRIORITY to use FreeRTOS from ISRs
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
  HAL_NVIC_SetPriority(DCMI_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(DCMI_IRQn);
  return true;
}

void Dcmi::setJpegMode(bool jpeg) {
  HAL_DCMI_DeInit(&hdcmi);
  dcmiInit(jpeg);
}

bool Dcmi::captureFrame(uint8_t *buf, uint32_t len, uint32_t timeoutMs) {
  frameError = false;
  waitingTask = xTaskGetCurrentTaskHandle();
  // HAL splits transfers longer than 65535 words into a double-buffered chain automatically.
  if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_SNAPSHOT, (uint32_t)buf, len / 4) != HAL_OK) {
    waitingTask = nullptr;
    return false;
  }
  bool got = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeoutMs)) > 0;
  waitingTask = nullptr;
  if (!got) HAL_DCMI_Stop(&hdcmi);
  // Keep the CPU view coherent if the data cache is ever enabled.
  SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t *>(buf), (int32_t)len);
  return got && !frameError;
}

uint32_t Dcmi::jpegLength(const uint8_t *buf, uint32_t max) {
  if (max < 4 || buf[0] != 0xFF || buf[1] != 0xD8) return 0;  // no SOI marker
  for (uint32_t i = max - 2; i > 2; i--) {
    if (buf[i] == 0xFF && buf[i + 1] == 0xD9) return i + 2;
  }
  return 0;
}

static void wakeWaiter() {
  if (!waitingTask) return;
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(waitingTask, &woken);
  portYIELD_FROM_ISR(woken);
}

extern "C" {
void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *h) {
  HAL_DCMI_Stop(h);
  wakeWaiter();
}
void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *h) {
  frameError = true;
  HAL_DCMI_Stop(h);
  wakeWaiter();
}
void DCMI_IRQHandler(void) { HAL_DCMI_IRQHandler(&hdcmi); }
void DMA2_Stream0_IRQHandler(void) { HAL_DMA_IRQHandler(&hdmaDcmi); }
}
