/**
 ******************************************************************************
 * @file            : bme280.c
 * @brief          : BME280 Temperature, Pressure, and Humidity Sensor Library
 * @author         : Siratul Islam
 * @version        : 1.0.0
 * @date           : May 30, 2025
 * @license        : MIT License
 * 
 * @description    : STM32 HAL-based driver for Bosch BME280 environmental sensor
 *                   Supports I2C communication with configurable oversampling,
 *                   filtering, and operating modes. Includes official Bosch
 *                   compensation algorithms for accurate measurements.
 * 
 * Copyright (c) 2025 Siratul Islam
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "bme280.h"

/* Private variables ---------------------------------------------------------*/
I2C_Safe_Bus_t *bme280_i2c_handle;
BME280_Calibration_TypeDef calibration_data;
BME280_RawData_TypeDef sensor_data;
BME280_S32_t t_fine;

void bme280_set_i2c_handle(I2C_Safe_Bus_t *bus) {
	bme280_i2c_handle = bus;
}

void read_sensor_id() {
	uint8_t buf[1];

	I2C_Safe_Read(bme280_i2c_handle, BME280_I2C_ADDRESS, 0xD0, 1, buf, 1);

}
void read_calibration_data() {
	uint8_t buf[33] = { 0 };

	buf[0] = BME280_REG_CALIB_TEMP_PRESS;

	// Read main calibration block (0x88-0xA1, includes 1 reserved byte)
	if (I2C_Safe_Read(bme280_i2c_handle, BME280_I2C_ADDRESS,
	BME280_REG_CALIB_TEMP_PRESS, 1, buf, 26) != HAL_OK) {
		return;
	}

	// Read humidity calibration MSB (0xE1)
	if (I2C_Safe_Read(bme280_i2c_handle, BME280_I2C_ADDRESS,
			BME280_REG_CALIB_HUMIDITY, 1, &buf[26], 7) != HAL_OK) {
		return;
	}

	//	Note: Left-shift by 8 bit to make room for the other 8-bit; combined it will be 16-bit

	// Temperature calibration coefficients (0x88-0x8D)
	calibration_data.dig_T1 = (uint16_t)((buf[1] << 8) | buf[0]);
	calibration_data.dig_T2 = (int16_t)((buf[3] << 8) | buf[2]);
	calibration_data.dig_T3 = (int16_t)((buf[5] << 8) | buf[4]);

	// Pressure calibration coefficients (0x8E-0x9F)
	calibration_data.dig_P1 = (uint16_t)((buf[7] << 8) | buf[6]);
	calibration_data.dig_P2 = (int16_t)((buf[9] << 8) | buf[8]);
	calibration_data.dig_P3 = (int16_t)((buf[11] << 8) | buf[10]);
	calibration_data.dig_P4 = (int16_t)((buf[13] << 8) | buf[12]);
	calibration_data.dig_P5 = (int16_t)((buf[15] << 8) | buf[14]);
	calibration_data.dig_P6 = (int16_t)((buf[17] << 8) | buf[16]);
	calibration_data.dig_P7 = (int16_t)((buf[19] << 8) | buf[18]);
	calibration_data.dig_P8 = (int16_t)((buf[21] << 8) | buf[20]);
	calibration_data.dig_P9 = (int16_t)((buf[23] << 8) | buf[22]);

	// Humidity calibration H1 (0xA1)
	calibration_data.dig_H1 = buf[25];

	// Humidity calibration H2-H6 (0xE1-0xE7)
	calibration_data.dig_H2 = (int16_t)((buf[27] << 8) | buf[26]);
	calibration_data.dig_H3 = buf[28];

	// H4 and H5 are split across bytes (see datasheet)
	calibration_data.dig_H4 = (int16_t)((((int8_t)buf[29]) * 16) | ((int16_t)(buf[30] & 0x0F)));
	calibration_data.dig_H5 = (int16_t)((((int8_t)buf[31]) * 16) | ((int16_t)(buf[30] >> 4)));

	calibration_data.dig_H6 = (int8_t)buf[32];
}

void read_sensor_data() {
	uint8_t buf[8];

	// Read all sensor data (0xF7-0xFE)
	if (I2C_Safe_Read(bme280_i2c_handle, BME280_I2C_ADDRESS,
			BME280_REG_PRESSURE_MSB, 1, buf, 8) != HAL_OK) {
		return;
	}

	// Pressure (20-bit): combine 3 bytes, shift xlsb right by 4
	sensor_data.pressure = ((uint32_t) buf[0] << 12) | ((uint32_t) buf[1] << 4)
			| (buf[2] >> 4);

	// Temperature (20-bit): combine 3 bytes, shift xlsb right by 4
	sensor_data.temperature = ((uint32_t) buf[3] << 12)
			| ((uint32_t) buf[4] << 4) | (buf[5] >> 4);

	// Humidity (16-bit): combine 2 bytes
	sensor_data.humidity = ((uint16_t) buf[6] << 8) | buf[7];

}

int bme280_init(I2C_Safe_Bus_t *bus, uint8_t humidity_oversampling,
		uint8_t temp_oversampling, uint8_t pressure_oversampling,
		uint8_t sensor_mode, uint8_t standby_time, uint8_t filter_coeff) {

	//	Set the i2c handle
	bme280_set_i2c_handle(bus);

	uint8_t write_data = 0;

	// Reset the device first
	write_data = 0xB6;  // reset sequence
	if (I2C_Safe_Write(bme280_i2c_handle, BME280_I2C_ADDRESS, 0xE0, 1,
			&write_data, 1) != HAL_OK) {
		return -1;
	}
	osDelay(100);

	// Read the values during initialization AFTER reset
	read_calibration_data();

	// Set humidity sampling (0xF2)
	write_data = humidity_oversampling;
	if (I2C_Safe_Write(bme280_i2c_handle, BME280_I2C_ADDRESS, 0xF2, 1,
			&write_data, 1) != HAL_OK) {
		return -1;
	}

	// Set config (0xF5) - standby time and filter
	write_data = (standby_time << 5) | (filter_coeff << 2);
	if (I2C_Safe_Write(bme280_i2c_handle, BME280_I2C_ADDRESS, 0xF5, 1,
			&write_data, 1) != HAL_OK) {
		return -1;
	}

	// Set temp/pressure sampling and mode (0xF4)
	write_data = (temp_oversampling << 5) | (pressure_oversampling << 2)
			| sensor_mode;
	if (I2C_Safe_Write(bme280_i2c_handle, BME280_I2C_ADDRESS, 0xF4, 1,
			&write_data, 1) != HAL_OK) {
		return -1;
	}

	return 0;  // Success
}

float bme280_get_temperature(void) {
	BME280_S32_t temp_raw = BME280_compensate_T_int32(sensor_data.temperature);
	return temp_raw / 100.0f; // Convert to degrees Celsius
}

float bme280_get_pressure(void) {
	BME280_U32_t press_raw = BME280_compensate_P_int64(sensor_data.pressure);
	return press_raw / 256.0f; // Convert to Pa
}

float bme280_get_humidity(void) {
	BME280_U32_t hum_raw = bme280_compensate_H_int32(sensor_data.humidity);
	return hum_raw / 1024.0f;
}

static uint8_t is_bme280_initialized = 0;

BME280_Telemetry_t bme280_easy_read(I2C_Safe_Bus_t *bus) {
	BME280_Telemetry_t data;
	
	if (!is_bme280_initialized) {
		int result = bme280_init(bus, BME280_OVERSAMPLE_X1, BME280_OVERSAMPLE_X1, BME280_OVERSAMPLE_X1, BME280_NORMAL_MODE, BME280_STANDBY_1000MS, BME280_FILTER_OFF);
		if (result == 0) {
			is_bme280_initialized = 1;
		} else {
			data.temperature = 0.0f;
			data.pressure = 0.0f;
			data.humidity = 0.0f;
			return data;
		}
	}

	read_sensor_data();

	data.temperature = bme280_get_temperature();
	data.pressure = bme280_get_pressure();
	data.humidity = bme280_get_humidity();

	return data;
}

// Functions taken from BME280 Data sheet

// Returns temperature in DegC, resolution is 0.01 DegC. Output value of "5123" equals 51.23 DegC.
// t_fine carries fine temperature as global value
BME280_S32_t BME280_compensate_T_int32(BME280_S32_t adc_T) {
	BME280_S32_t var1, var2, T;
	var1 = ((((adc_T >> 3) - ((BME280_S32_t) calibration_data.dig_T1 << 1)))
			* ((BME280_S32_t) calibration_data.dig_T2)) >> 11;
	var2 = (((((adc_T >> 4) - ((BME280_S32_t) calibration_data.dig_T1))
			* ((adc_T >> 4) - ((BME280_S32_t) calibration_data.dig_T1))) >> 12)
			* ((BME280_S32_t) calibration_data.dig_T3)) >> 14;
	t_fine = var1 + var2;
	T = (t_fine * 5 + 128) >> 8;
	return T;
}
// Returns pressure in Pa as unsigned 32 bit integer in Q24.8 format (24 integer bits and 8 fractional bits).
// Output value of "24674867" represents 24674867/256 = 96386.2 Pa = 963.862 hPa
BME280_U32_t BME280_compensate_P_int64(BME280_S32_t adc_P) {
	BME280_S64_t var1, var2, p;
	var1 = ((BME280_S64_t) t_fine) - 128000;
	var2 = var1 * var1 * (BME280_S64_t) calibration_data.dig_P6;
	var2 = var2 + ((var1 * (BME280_S64_t) calibration_data.dig_P5) << 17);
	var2 = var2 + (((BME280_S64_t) calibration_data.dig_P4) << 35);
	var1 = ((var1 * var1 * (BME280_S64_t) calibration_data.dig_P3) >> 8)
			+ ((var1 * (BME280_S64_t) calibration_data.dig_P2) << 12);
	var1 = (((((BME280_S64_t) 1) << 47) + var1))
			* ((BME280_S64_t) calibration_data.dig_P1) >> 33;
	if (var1 == 0) {
		return 0; // avoid exception caused by division by zero
	}
	p = 1048576 - adc_P;
	p = (((p << 31) - var2) * 3125) / var1;
	var1 = (((BME280_S64_t) calibration_data.dig_P9) * (p >> 13) * (p >> 13))
			>> 25;
	var2 = (((BME280_S64_t) calibration_data.dig_P8) * p) >> 19;
	p = ((p + var1 + var2) >> 8)
			+ (((BME280_S64_t) calibration_data.dig_P7) << 4);
	return (BME280_U32_t) p;
}

// Returns humidity in %RH as unsigned 32 bit integer in Q22.10 format (22 integer and 10 fractional bits).
// Output value of "47445" represents 47445/1024 = 46.333 %RH
BME280_U32_t bme280_compensate_H_int32(BME280_S32_t adc_H) {
	BME280_S32_t v_x1_u32r;
	v_x1_u32r = (t_fine - ((BME280_S32_t) 76800));
	v_x1_u32r =
			(((((adc_H << 14) - (((BME280_S32_t) calibration_data.dig_H4) << 20)
					- (((BME280_S32_t) calibration_data.dig_H5) * v_x1_u32r))
					+ ((BME280_S32_t) 16384)) >> 15)
					* (((((((v_x1_u32r
							* ((BME280_S32_t) calibration_data.dig_H6)) >> 10)
							* (((v_x1_u32r
									* ((BME280_S32_t) calibration_data.dig_H3))
									>> 11) + ((BME280_S32_t) 32768))) >> 10)
							+ ((BME280_S32_t) 2097152))
							* ((BME280_S32_t) calibration_data.dig_H2) + 8192)
							>> 14));
	v_x1_u32r = (v_x1_u32r
			- (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7)
					* ((BME280_S32_t) calibration_data.dig_H1)) >> 4));
	v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
	v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
	return (BME280_U32_t) (v_x1_u32r >> 12);
}
