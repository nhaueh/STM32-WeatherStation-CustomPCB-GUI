/**
 * @author  Alexander Hoffman
 * @email   alxhoff@gmail.com
 * @website http://alexhoffman.info
 * @license GNU GPL v3
 * @brief	STM32 HAL library for BH1750 devices
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

#include <stdlib.h>

#include "BH1750.h"

static BH1750_device_t* g_bh1750_easy = NULL;

HAL_StatusTypeDef BH1750_init_i2c(I2C_HandleTypeDef* i2c_handle)
{
	/*
	 * Deprecated in shared-bus projects.
	 * I2C is initialized centrally by CubeMX (MX_I2C1_Init).
	 */
	(void)i2c_handle;
	return HAL_OK;
}

static HAL_StatusTypeDef BH1750_send_command(BH1750_device_t* dev, uint8_t cmd)
{
	if ((dev == NULL) || (dev->i2c_handle == NULL)) return HAL_ERROR;

	if (dev->safe_bus != NULL)
	{
		return I2C_Safe_Master_Transmit(dev->safe_bus, dev->address, &cmd, 1);
	}

	if (HAL_I2C_Master_Transmit(dev->i2c_handle,
			dev->address,
			&cmd,
			1,
			BH1750_I2C_TIMEOUT_MS) != HAL_OK) return HAL_ERROR;

	return HAL_OK;
}

void BH1750_poll_self(BH1750_device_t* self)
{
	BH1750_get_lumen(self);
}

BH1750_device_t* BH1750_init_dev_struct(I2C_HandleTypeDef* i2c_handle,
		const char* name, bool addr_grounded)
{
	BH1750_device_t* init =
			(BH1750_device_t*)calloc(1, sizeof(BH1750_device_t));

	if(init == NULL) return NULL;

	if(addr_grounded){
		init->address = BH1750_GROUND_ADDR;
	}else{
		init->address = BH1750_NO_GROUND_ADDR;
	}

	init->i2c_handle = i2c_handle;
	init->safe_bus = NULL;
	init->name = name;
	init->mode = CMD_H_RES_MODE;

	init->poll = &BH1750_poll_self;

	return init;
}

void BH1750_attach_safe_bus(BH1750_device_t* dev, I2C_Safe_Bus_t* safe_bus)
{
	if (dev == NULL) return;
	dev->safe_bus = safe_bus;
	if ((safe_bus != NULL) && (safe_bus->handle != NULL))
		dev->i2c_handle = safe_bus->handle;
}

HAL_StatusTypeDef BH1750_init_dev(BH1750_device_t* dev)
{
	if (dev == NULL) return HAL_ERROR;

	if (BH1750_send_command(dev, CMD_POWER_ON) != HAL_OK) return HAL_ERROR;
	if (BH1750_send_command(dev, CMD_RESET) != HAL_OK) return HAL_ERROR;
	if (BH1750_send_command(dev, dev->mode) != HAL_OK) return HAL_ERROR;

	return HAL_OK;
}

HAL_StatusTypeDef BH1750_read_dev(BH1750_device_t* dev)
{
	if ((dev == NULL) || (dev->i2c_handle == NULL)) return HAL_ERROR;

	if (dev->safe_bus != NULL)
	{
		return I2C_Safe_Master_Receive(dev->safe_bus, dev->address, dev->buffer, 2);
	}

	if(HAL_I2C_Master_Receive(dev->i2c_handle,
			dev->address,
			dev->buffer,
			2,
			BH1750_I2C_TIMEOUT_MS
	) != HAL_OK) return HAL_ERROR;

	return HAL_OK;
}

HAL_StatusTypeDef BH1750_convert(BH1750_device_t* dev)
{
	uint16_t raw;

	if (dev == NULL) return HAL_ERROR;

	dev->value = dev->buffer[0];
	dev->value = (dev->value << 8) | dev->buffer[1];

	raw = dev->value;
	dev->value = (uint16_t)((raw * 10U) / 12U);

	return HAL_OK;
}

HAL_StatusTypeDef BH1750_get_lumen(BH1750_device_t* dev)
{
	if (BH1750_read_dev(dev) != HAL_OK) return HAL_ERROR;
	if (BH1750_convert(dev) != HAL_OK) return HAL_ERROR;
	return HAL_OK;
}

HAL_StatusTypeDef BH1750_easy_init(I2C_Safe_Bus_t* safe_bus, bool addr_grounded)
{
	if ((safe_bus == NULL) || (safe_bus->handle == NULL)) return HAL_ERROR;

	if (g_bh1750_easy == NULL)
	{
		g_bh1750_easy = BH1750_init_dev_struct(safe_bus->handle, "BH1750", addr_grounded);
		if (g_bh1750_easy == NULL) return HAL_ERROR;
	}

	BH1750_attach_safe_bus(g_bh1750_easy, safe_bus);
	return BH1750_init_dev(g_bh1750_easy);
}

HAL_StatusTypeDef BH1750_easy_read_lux(uint16_t* lux)
{
	if ((g_bh1750_easy == NULL) || (lux == NULL)) return HAL_ERROR;

	if (BH1750_get_lumen(g_bh1750_easy) != HAL_OK) return HAL_ERROR;

	*lux = g_bh1750_easy->value;
	return HAL_OK;
}

uint16_t BH1750_easy_read_lux_or_zero(void)
{
	uint16_t lux = 0U;
	(void)BH1750_easy_read_lux(&lux);
	return lux;
}
