#include "i2c_safe.h"

static void i2c_safe_update_error_state(I2C_Safe_Bus_t *bus, HAL_StatusTypeDef hal_status)
{
    if (bus == NULL)
    {
        return;
    }

    if (hal_status == HAL_OK)
    {
        bus->consecutive_errors = 0U;
        return;
    }

    if (bus->consecutive_errors < 0xFFU)
    {
        bus->consecutive_errors++;
    }

    if (bus->consecutive_errors >= I2C_SAFE_RECOVERY_THRESHOLD)
    {
        (void)HAL_I2C_DeInit(bus->handle);
        if (HAL_I2C_Init(bus->handle) == HAL_OK)
        {
            bus->consecutive_errors = 0U;
        }
    }
}

static HAL_StatusTypeDef i2c_safe_lock(const I2C_Safe_Bus_t *bus)
{
    osStatus lock_status;

    if ((bus == NULL) || (bus->mutex == NULL))
    {
        return HAL_ERROR;
    }

    lock_status = osMutexAcquire(bus->mutex, I2C_SAFE_MUTEX_TIMEOUT_MS);
    if (lock_status == osOK)
    {
        return HAL_OK;
    }

#ifdef osErrorTimeoutResource
    if (lock_status == osErrorTimeoutResource)
    {
        return HAL_TIMEOUT;
    }
#endif

    return HAL_ERROR;
}

void I2C_Safe_Init(I2C_Safe_Bus_t *bus,
                   I2C_HandleTypeDef *handle,
                   osMutexId_t mutex,
                   uint32_t hal_timeout_ms)
{
    if (bus == NULL)
    {
        return;
    }

    bus->handle = handle;
    bus->mutex = mutex;
    bus->hal_timeout_ms = (hal_timeout_ms == 0U) ? I2C_SAFE_HAL_TIMEOUT_MS : hal_timeout_ms;
    bus->consecutive_errors = 0U;
}

HAL_StatusTypeDef I2C_Safe_Recover(I2C_Safe_Bus_t *bus)
{
    HAL_StatusTypeDef lock_status;
    HAL_StatusTypeDef hal_status;

    if ((bus == NULL) || (bus->handle == NULL))
    {
        return HAL_ERROR;
    }

    lock_status = i2c_safe_lock(bus);
    if (lock_status != HAL_OK)
    {
        return lock_status;
    }

    (void)HAL_I2C_DeInit(bus->handle);
    hal_status = HAL_I2C_Init(bus->handle);

    if (osMutexRelease(bus->mutex) != osOK)
    {
        return (hal_status == HAL_OK) ? HAL_ERROR : hal_status;
    }

    if (hal_status == HAL_OK)
    {
        bus->consecutive_errors = 0U;
        return HAL_OK;
    }

    return HAL_ERROR;
}

HAL_StatusTypeDef I2C_Safe_Write(I2C_Safe_Bus_t *bus,
                                 uint16_t dev_addr,
                                 uint16_t mem_addr,
                                 uint16_t mem_addr_size,
                                 const uint8_t *data,
                                 uint16_t size)
{
    HAL_StatusTypeDef hal_status;
    HAL_StatusTypeDef lock_status;

    if ((bus == NULL) || (bus->handle == NULL) || (data == NULL) || (size == 0U))
    {
        return HAL_ERROR;
    }

    lock_status = i2c_safe_lock(bus);
    if (lock_status != HAL_OK)
    {
        return lock_status;
    }

    hal_status = HAL_I2C_Mem_Write(bus->handle,
                                   dev_addr,
                                   mem_addr,
                                   mem_addr_size,
                                   (uint8_t *)data,
                                   size,
                                   bus->hal_timeout_ms);

    i2c_safe_update_error_state(bus, hal_status);

    if (osMutexRelease(bus->mutex) != osOK)
    {
        return (hal_status == HAL_OK) ? HAL_ERROR : hal_status;
    }

    return hal_status;
}

HAL_StatusTypeDef I2C_Safe_Read(I2C_Safe_Bus_t *bus,
                                uint16_t dev_addr,
                                uint16_t mem_addr,
                                uint16_t mem_addr_size,
                                uint8_t *data,
                                uint16_t size)
{
    HAL_StatusTypeDef hal_status;
    HAL_StatusTypeDef lock_status;

    if ((bus == NULL) || (bus->handle == NULL) || (data == NULL) || (size == 0U))
    {
        return HAL_ERROR;
    }

    lock_status = i2c_safe_lock(bus);
    if (lock_status != HAL_OK)
    {
        return lock_status;
    }

    hal_status = HAL_I2C_Mem_Read(bus->handle,
                                  dev_addr,
                                  mem_addr,
                                  mem_addr_size,
                                  data,
                                  size,
                                  bus->hal_timeout_ms);

    i2c_safe_update_error_state(bus, hal_status);

    if (osMutexRelease(bus->mutex) != osOK)
    {
        return (hal_status == HAL_OK) ? HAL_ERROR : hal_status;
    }

    return hal_status;
}

HAL_StatusTypeDef I2C_Safe_Master_Transmit(I2C_Safe_Bus_t *bus,
                                           uint16_t dev_addr,
                                           const uint8_t *data,
                                           uint16_t size)
{
    HAL_StatusTypeDef hal_status;
    HAL_StatusTypeDef lock_status;

    if ((bus == NULL) || (bus->handle == NULL) || (data == NULL) || (size == 0U))
    {
        return HAL_ERROR;
    }

    lock_status = i2c_safe_lock(bus);
    if (lock_status != HAL_OK)
    {
        return lock_status;
    }

    hal_status = HAL_I2C_Master_Transmit(bus->handle,
                                         dev_addr,
                                         (uint8_t *)data,
                                         size,
                                         bus->hal_timeout_ms);

    i2c_safe_update_error_state(bus, hal_status);

    if (osMutexRelease(bus->mutex) != osOK)
    {
        return (hal_status == HAL_OK) ? HAL_ERROR : hal_status;
    }

    return hal_status;
}

HAL_StatusTypeDef I2C_Safe_Master_Receive(I2C_Safe_Bus_t *bus,
                                          uint16_t dev_addr,
                                          uint8_t *data,
                                          uint16_t size)
{
    HAL_StatusTypeDef hal_status;
    HAL_StatusTypeDef lock_status;

    if ((bus == NULL) || (bus->handle == NULL) || (data == NULL) || (size == 0U))
    {
        return HAL_ERROR;
    }

    lock_status = i2c_safe_lock(bus);
    if (lock_status != HAL_OK)
    {
        return lock_status;
    }

    hal_status = HAL_I2C_Master_Receive(bus->handle,
                                        dev_addr,
                                        data,
                                        size,
                                        bus->hal_timeout_ms);

    i2c_safe_update_error_state(bus, hal_status);

    if (osMutexRelease(bus->mutex) != osOK)
    {
        return (hal_status == HAL_OK) ? HAL_ERROR : hal_status;
    }

    return hal_status;
}
