/**
 * @author  Alexander Hoffman
 * @email   alxhoff@gmail.com
 * @website http://alexhoffman.info
 * @license GNU GPL v3
 * @brief   STM32 HAL library for BH1750 devices
 *
@verbatim
   ----------------------------------------------------------------------
    Copyright (C) Alexander Hoffman, 2017

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
   ----------------------------------------------------------------------
@endverbatim
 */

#ifndef BH1750_H_
#define BH1750_H_

#include <stdbool.h>
#include <stdint.h>

#include "stm32f1xx_hal.h"
#include "i2c_safe.h"

// Device Address
// HAL I2C expects 7-bit address shifted left 1 bit.
#define BH1750_GROUND_ADDR         (0x23U << 1)   // ADDR pin = GND
#define BH1750_NO_GROUND_ADDR      (0x5CU << 1)   // ADDR pin = VCC

//instructions
//datasheet ref http://cpre.kmutnb.ac.th/esl/learning/bh1750-light-sensor/bh1750fvi-e_datasheet.pdf
#define CMD_POWER_DOWN          0x00
#define CMD_POWER_ON            0x01
#define CMD_RESET               0x03
#define CMD_H_RES_MODE          0x10
#define CMD_H_RES_MODE2         0x11
#define CMD_L_RES_MODE          0x13
#define CMD_ONE_H_RES_MODE      0x20
#define CMD_ONE_H_RES_MODE2     0x21
#define CMD_ONE_L_RES_MODE      0x23
#define CMD_CNG_TIME_HIGH       0x30    // 3 LSB set time
#define CMD_CNG_TIME_LOW        0x60    // 5 LSB set time

#define BH1750_I2C_TIMEOUT_MS      100U

typedef struct BH1750_device BH1750_device_t;
struct BH1750_device{
    const char* name;

    I2C_HandleTypeDef* i2c_handle;
    I2C_Safe_Bus_t* safe_bus;
    uint16_t address;

    uint16_t value;

    uint8_t buffer[2];

    uint8_t mode;

    void (* poll)(BH1750_device_t*);
} ;

HAL_StatusTypeDef BH1750_read_dev(BH1750_device_t* dev);
HAL_StatusTypeDef BH1750_init_i2c(I2C_HandleTypeDef* i2c_handle);
BH1750_device_t* BH1750_init_dev_struct(I2C_HandleTypeDef* i2c_handle,
    const char* name, bool addr_grounded);
HAL_StatusTypeDef BH1750_init_dev(BH1750_device_t* dev);
HAL_StatusTypeDef BH1750_get_lumen(BH1750_device_t* dev);
void BH1750_attach_safe_bus(BH1750_device_t* dev, I2C_Safe_Bus_t* safe_bus);

/* Easy wrapper API (clean FreeRTOS usage)
 * Init once:  BH1750_easy_init(&g_i2c1_safe_bus, true);
 * Read fast:  lux = BH1750_easy_read_lux_or_zero();
 */
HAL_StatusTypeDef BH1750_easy_init(I2C_Safe_Bus_t* safe_bus, bool addr_grounded);
HAL_StatusTypeDef BH1750_easy_read_lux(uint16_t* lux);
uint16_t BH1750_easy_read_lux_or_zero(void);

#endif /* BH1750_H_ */
