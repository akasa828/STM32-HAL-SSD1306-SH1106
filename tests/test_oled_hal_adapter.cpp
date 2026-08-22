#include <cstdint>
#include <iostream>

#include "oled.hpp"
#include "oled_stm32_hal.h"

namespace {

HAL_StatusTypeDef g_write_status = HAL_OK;
HAL_StatusTypeDef g_device_status = HAL_OK;
uint32_t g_tick = 0;
uint32_t g_writes = 0;
uint32_t g_aborts = 0;
uint32_t g_deinits = 0;
uint32_t g_reinitializations = 0;
uint32_t g_successes = 0;
uint32_t g_failures = 0;

void reset_hal()
{
    g_write_status = HAL_OK;
    g_device_status = HAL_OK;
    g_tick = 0;
    g_writes = 0;
    g_aborts = 0;
    g_deinits = 0;
    g_reinitializations = 0;
    g_successes = 0;
    g_failures = 0;
}

void reinitialize(void *, I2C_HandleTypeDef *i2c)
{
    ++g_reinitializations;
    i2c->state = HAL_I2C_STATE_READY;
}

void success(void *)
{
    ++g_successes;
}

void failure(void *, uint8_t)
{
    ++g_failures;
}

bool expect(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

OLED_STM32_HAL make_adapter(I2C_HandleTypeDef &i2c)
{
    OLED_STM32_HAL adapter{};
    adapter.i2c = &i2c;
    adapter.reinitialize = reinitialize;
    adapter.success = success;
    adapter.failure = failure;
    return adapter;
}

}

extern "C" HAL_StatusTypeDef HAL_I2C_Mem_Write_DMA(
    I2C_HandleTypeDef *, uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t)
{
    ++g_writes;
    return g_write_status;
}

extern "C" HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *)
{
    ++g_aborts;
    return HAL_OK;
}

extern "C" HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *i2c)
{
    ++g_deinits;
    i2c->state = HAL_I2C_STATE_RESET;
    return HAL_OK;
}

extern "C" HAL_I2C_StateTypeDef HAL_I2C_GetState(I2C_HandleTypeDef *i2c)
{
    return i2c->state;
}

extern "C" HAL_StatusTypeDef HAL_I2C_IsDeviceReady(
    I2C_HandleTypeDef *, uint16_t, uint32_t, uint32_t)
{
    return g_device_status;
}

extern "C" uint32_t HAL_GetTick(void)
{
    return g_tick++;
}

int main()
{
    reset_hal();
    DMA_HandleTypeDef dma{};
    I2C_HandleTypeDef i2c{&dma, HAL_I2C_STATE_READY};
    I2C_HandleTypeDef other{&dma, HAL_I2C_STATE_READY};
    OLED_STM32_HAL adapter = make_adapter(i2c);

    if (!expect(OLED_STM32_HAL_Attach(&adapter) == OLED_PORT_OK,
                "attach valid adapter")) return 1;
    OLED_Write_Byte(0xAE, CMD);
    if (!expect(adapter.transfer_active == 1U && OLED_DMA_Busy == 1U,
                "successful DMA start must mark the transfer active")) return 1;

    OLED_STM32_HAL_HandleTxComplete(&adapter, &other);
    if (!expect(adapter.transfer_active == 1U && OLED_DMA_Busy == 1U,
                "an unrelated I2C callback must be ignored")) return 1;
    OLED_STM32_HAL_HandleTxComplete(&adapter, &i2c);
    if (!expect(adapter.transfer_active == 0U && OLED_DMA_Busy == 0U,
                "the matching completion must finish the active transfer")) return 1;
    if (!expect(g_successes == 1U, "completion callback count")) return 1;
    OLED_STM32_HAL_HandleTxComplete(&adapter, &i2c);
    if (!expect(g_successes == 1U, "duplicate completion must be ignored")) return 1;

    g_write_status = HAL_TIMEOUT;
    OLED_Write_Byte(0xAF, CMD);
    if (!expect(g_aborts == 1U && g_deinits == 1U && g_reinitializations == 1U,
                "HAL timeout must abort and reinitialize I2C")) return 1;
    if (!expect(adapter.transfer_active == 0U && OLED_DMA_Busy == 0U,
                "failed DMA start must clear active state")) return 1;

    reset_hal();
    i2c.state = HAL_I2C_STATE_READY;
    adapter = make_adapter(i2c);
    if (!expect(OLED_STM32_HAL_Attach(&adapter) == OLED_PORT_OK,
                "attach adapter without metric callbacks")) return 1;
    g_write_status = HAL_TIMEOUT;
    OLED_Write_Byte(0xAF, CMD);
    if (!expect(OLED_Get_I2C_Error_Count() == 1U &&
                OLED_Get_I2C_Timeout_Count() == 1U,
                "missing metric callbacks must fall back to core counters")) return 1;

    reset_hal();
    i2c.state = HAL_I2C_STATE_READY;
    adapter = make_adapter(i2c);
    adapter.reinitialize = nullptr;
    if (!expect(OLED_STM32_HAL_Attach(&adapter) == OLED_PORT_OK,
                "attach adapter without optional recovery callback")) return 1;
    g_write_status = HAL_ERROR;
    OLED_Write_Byte(0xAE, CMD);
    if (!expect(g_deinits == 0U,
                "missing reinitializer must not leave I2C deinitialized")) return 1;

    reset_hal();
    i2c.state = HAL_I2C_STATE_READY;
    adapter = make_adapter(i2c);
    OLED_STM32_HAL_Attach(&adapter);
    OLED_Write_Byte(0xAE, CMD);
    OLED_STM32_HAL_HandleError(&adapter, &i2c);
    if (!expect(g_failures == 1U, "active HAL error must be reported")) return 1;
    OLED_Wait_DMA();
    if (!expect(g_aborts == 1U && g_reinitializations == 1U,
                "active HAL error must recover on the next wait")) return 1;

    std::cout << "OLED HAL adapter tests passed\n";
    return 0;
}
