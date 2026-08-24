#ifndef STM32F1XX_HAL_H
#define STM32F1XX_HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_BUSY = 2,
    HAL_TIMEOUT = 3
} HAL_StatusTypeDef;

typedef enum {
    HAL_I2C_STATE_RESET = 0,
    HAL_I2C_STATE_READY = 1,
    HAL_I2C_STATE_BUSY = 2
} HAL_I2C_StateTypeDef;

typedef struct {
    uint32_t marker;
} DMA_HandleTypeDef;

typedef struct {
    uint32_t AddressingMode;
} I2C_InitTypeDef;

typedef struct {
    DMA_HandleTypeDef *hdmatx;
    HAL_I2C_StateTypeDef state;
    I2C_InitTypeDef Init;
} I2C_HandleTypeDef;

#define I2C_MEMADD_SIZE_8BIT 1U
#define I2C_ADDRESSINGMODE_7BIT 0x00004000U
#define I2C_ADDRESSINGMODE_10BIT 0x0000C000U

HAL_StatusTypeDef HAL_I2C_Mem_Write_DMA(I2C_HandleTypeDef *i2c,
                                        uint16_t address,
                                        uint16_t control,
                                        uint16_t address_size,
                                        uint8_t *data,
                                        uint16_t size);
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *dma);
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *i2c);
HAL_I2C_StateTypeDef HAL_I2C_GetState(I2C_HandleTypeDef *i2c);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *i2c,
                                        uint16_t address,
                                        uint32_t trials,
                                        uint32_t timeout_ms);
uint32_t HAL_GetTick(void);

#ifdef __cplusplus
}
#endif

#endif
