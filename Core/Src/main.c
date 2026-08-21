#include "main.h"
#include "dma.h"
#include "gpio.h"
#include "i2c.h"
#include "oled.hpp"
#include "oled_stm32_hal.h"

volatile uint32_t g_fatal_error_marker;
static OLED_STM32_HAL s_oled_hal;

void SystemClock_Config(void);

static void oled_reinitialize(void *context, I2C_HandleTypeDef *i2c)
{
    (void)context;
    (void)i2c;
    MX_I2C1_Init();
}

static void oled_success(void *context) { (void)context; I2C1_AdaptiveSuccess(); }
static void oled_failure(void *context, uint8_t timeout)
{ (void)context; I2C1_AdaptiveFailure(timeout); }
static uint32_t oled_clock(void *context) { (void)context; return I2C1_GetClockHz(); }
static uint32_t oled_errors(void *context) { (void)context; return I2C1_GetErrorCount(); }
static uint32_t oled_timeouts(void *context) { (void)context; return I2C1_GetTimeoutCount(); }

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_I2C1_Init();

    s_oled_hal.i2c = &hi2c1;
    s_oled_hal.reinitialize = oled_reinitialize;
    s_oled_hal.success = oled_success;
    s_oled_hal.failure = oled_failure;
    s_oled_hal.clock_hz = oled_clock;
    s_oled_hal.error_count = oled_errors;
    s_oled_hal.timeout_count = oled_timeouts;
    if (OLED_STM32_HAL_Attach(&s_oled_hal) != OLED_PORT_OK) Error_Handler();

    OLED_Init();

    uint16_t position = 0U;
    uint16_t travel = OLED_WIDTH > 10U ? (uint16_t)(OLED_WIDTH - 10U) : 1U;
    for (;;) {
        OLED_GRAM_Clear();
        OLED_Draw_Rectang(0, 0, OLED_WIDTH - 1, OLED_HEIGHT - 1, 0);
        if (OLED_WIDTH >= 72U && OLED_HEIGHT >= 24U) {
            OLED_Show_String("OLED DRIVER", "1206", 3, 3);
            OLED_Show_String("PB6:SCL PB7:SDA", "0806", 3, 17);
        }
        if (OLED_HEIGHT >= 12U)
            OLED_Draw_Rectang((int16_t)position + 1, OLED_HEIGHT - 10, 7, 7, 1);
        OLED_Swap_Buffers();
        position = (uint16_t)((position + 1U) % travel);
        HAL_Delay(16U);
    }
}

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *i2c)
{
    OLED_STM32_HAL_HandleTxComplete(&s_oled_hal, i2c);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *i2c)
{
    OLED_STM32_HAL_HandleError(&s_oled_hal, i2c);
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.HSIState = RCC_HSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

void Error_Handler(void)
{
    g_fatal_error_marker = 0x45525221UL;
    __disable_irq();
    for (;;) { }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    Error_Handler();
}
#endif
