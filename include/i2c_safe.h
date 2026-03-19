#ifndef __I2C_SAFE_H__
#define __I2C_SAFE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "i2c.h"
#include "cmsis_os.h"

/*
 * Compatibility layer:
 * - CMSIS-RTOS v2: osMutexId_t + osMutexAcquire/osMutexRelease
 * - CMSIS-RTOS v1: osMutexId  + osMutexWait/osMutexRelease
 */
#ifndef osMutexId_t
typedef osMutexId osMutexId_t;
#endif

#ifndef osMutexAcquire
#define osMutexAcquire(mutex_id, timeout) osMutexWait((mutex_id), (timeout))
#endif

#define I2C_SAFE_MUTEX_TIMEOUT_MS  120U
#define I2C_SAFE_HAL_TIMEOUT_MS    100U
#define I2C_SAFE_RECOVERY_THRESHOLD 3U

typedef struct
{
    I2C_HandleTypeDef *handle;
    osMutexId_t mutex;
    uint32_t hal_timeout_ms;
    uint8_t consecutive_errors;
} I2C_Safe_Bus_t;

void I2C_Safe_Init(I2C_Safe_Bus_t *bus,
                   I2C_HandleTypeDef *handle,
                   osMutexId_t mutex,
                   uint32_t hal_timeout_ms);

HAL_StatusTypeDef I2C_Safe_Write(I2C_Safe_Bus_t *bus,
                                 uint16_t dev_addr,
                                 uint16_t mem_addr,
                                 uint16_t mem_addr_size,
                                 const uint8_t *data,
                                 uint16_t size);

HAL_StatusTypeDef I2C_Safe_Read(I2C_Safe_Bus_t *bus,
                                uint16_t dev_addr,
                                uint16_t mem_addr,
                                uint16_t mem_addr_size,
                                uint8_t *data,
                                uint16_t size);

HAL_StatusTypeDef I2C_Safe_Master_Transmit(I2C_Safe_Bus_t *bus,
                                           uint16_t dev_addr,
                                           const uint8_t *data,
                                           uint16_t size);

HAL_StatusTypeDef I2C_Safe_Master_Receive(I2C_Safe_Bus_t *bus,
                                          uint16_t dev_addr,
                                          uint8_t *data,
                                          uint16_t size);

HAL_StatusTypeDef I2C_Safe_Recover(I2C_Safe_Bus_t *bus);

#ifdef __cplusplus
}
#endif

#endif /* __I2C_SAFE_H__ */
