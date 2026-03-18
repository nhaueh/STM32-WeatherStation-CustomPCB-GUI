/**
 * @author  Alexander Hoffman
 * @email   alxhoff@gmail.com
 * @website http://alexhoffman.info
 * @license GNU GPL v3
 * @brief
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
#include <stdarg.h>
#include <stdio.h>

#include"ssd1306.h"

// # Handle OLED dùng cho API tiện lợi kiểu Arduino
static SSD1306_device_t* g_oled_easy = NULL;

// # Gửi 1 lệnh command cho SSD1306 qua I2C.
// # SSD1306 dùng control byte 0x00 cho command.
HAL_StatusTypeDef ssd1306_write_command(SSD1306_device_t* self, uint8_t command)
{
	if(HAL_I2C_Mem_Write(self->port,SSD1306_I2C_ADDR,0x00,1,&command,1,10) != HAL_OK)
		return HAL_ERROR;

	return HAL_OK;
}

// # Xóa buffer theo màu nền mặc định, sau đó update lên màn hình.
HAL_StatusTypeDef ssd1306_clear(SSD1306_device_t* self)
{
	uint32_t i;

	for(i = 0; i < sizeof(self->buffer); i++)
	{
		self->buffer[i] = (self->background == Black) ? 0x00 : 0xFF;
	}
	if(ssd1306_update_screen(self) != HAL_OK) return HAL_ERROR;

	return HAL_OK;
}

// # Fill toàn bộ framebuffer bằng 1 màu (đen/trắng),
// # chưa gửi ra OLED cho đến khi gọi update_screen().
HAL_StatusTypeDef ssd1306_fill(SSD1306_device_t* self, SSD1306_colour_t color)
{
	uint32_t i;

	for(i = 0; i < sizeof(self->buffer); i++)
	{
		self->buffer[i] = (color == Black) ? 0x00 : 0xFF;
	}

	return HAL_OK;
}

// # Gửi framebuffer lên OLED theo page (mỗi page = 8 hàng pixel).
// # width * 8 pages = full màn 128x64.
HAL_StatusTypeDef ssd1306_update_screen(SSD1306_device_t* self)
{
	uint8_t i;

	for (i = 0; i < 8; i++) {
		ssd1306_write_command(self, 0xB0 + i);
		/* # SH1106: set lower column start address with +2 offset */
		ssd1306_write_command(self, 0x02);
		/* # SH1106: set higher column start address */
		ssd1306_write_command(self, 0x10);

		if(HAL_I2C_Mem_Write(self->port,SSD1306_I2C_ADDR,0x40,1,
			&self->buffer[self->width * i], self->width,100) != HAL_OK)
			return HAL_ERROR;

//		HAL_I2C_Mem_Write(&hi2c1,SSD1306_I2C_ADDR,0x40,1,&SSD1306_Buffer[SSD1306_WIDTH * i],SSD1306_WIDTH,100);
	}
	return HAL_OK;
}

// # Vẽ 1 pixel vào buffer.
// # Vì buffer 1-bit nên tính byte index bằng: x + (y/8)*width.
HAL_StatusTypeDef ssd1306_draw_pixel(SSD1306_device_t* self, uint8_t x, uint8_t y, SSD1306_colour_t colour)
{
	// # Chặn out-of-range để tránh ghi bậy bộ nhớ.
	if (x >= self->width || y >= self->height)
	{
		return HAL_ERROR;
	}

	// # Set hoặc clear bit tương ứng trong byte.
	if (colour == White)
	{
		self->buffer[x + (y / 8) * self->width] |= 1 << (y % 8);
	} 
	else 
	{
		self->buffer[x + (y / 8) * self->width] &= ~(1 << (y % 8));
	}

	return HAL_OK;
}

// # Vẽ 1 ký tự từ bảng font vào buffer tại vị trí cursor (self->x, self->y).
HAL_StatusTypeDef ssd1306_write_char(SSD1306_device_t* self, char ch, FontDef Font, SSD1306_colour_t color)
{
	uint32_t i, b, j;
	
	// # Không đủ chỗ hiển thị ký tự thì trả fail.
	if (self->width <= (self->x + Font.FontWidth) ||
		self->height <= (self->y + Font.FontHeight))
	{
		return 0;
	}
	
	for (i = 0; i < Font.FontHeight; i++)
	{
		b = Font.data[(ch - 32) * Font.FontHeight + i];
		for (j = 0; j < Font.FontWidth; j++)
		{
			if ((b << j) & 0x8000) 
			{
				if(ssd1306_draw_pixel(self, self->x + j, (self->y + i),
						(SSD1306_colour_t) color) != HAL_OK)
					return HAL_ERROR;
			} 
			else 
			{
				if(ssd1306_draw_pixel(self, self->x + j, (self->y + i),
						(SSD1306_colour_t)!color) != HAL_OK)
					return HAL_ERROR;
			}
		}
	}
	
	self->x += Font.FontWidth;
	
	return HAL_OK;
}


	// # Ghi chuỗi ký tự liên tiếp bằng font hiện tại.
	// # Màu text được chọn ngược với background để dễ nhìn.
HAL_StatusTypeDef ssd1306_write_string(SSD1306_device_t* self, char* str)
{
	SSD1306_colour_t colour = 0x00;
	if(self->background == 0x00)
		colour = 0x01;
	else
		colour = 0x00;
	while (*str) 
	{
		if (ssd1306_write_char(self, *str, *self->font, colour) != HAL_OK)
		{
			return HAL_ERROR;
		}
		
		str++;
	}
	
	return HAL_OK;
}


// # Đặt con trỏ text.
void ssd1306_set_cursor(SSD1306_device_t* self, uint8_t x, uint8_t y)
{
	self->x = x;
	self->y = y;
}

// # Khởi tạo thiết bị:
// # 1) Cấp phát struct
// # 2) Bind function pointers
// # 3) Gửi chuỗi lệnh init SSD1306
// # 4) Fill và update màn hình lần đầu
SSD1306_device_t* ssd1306_init(SSD1306_device_init_t* init_dev_vals)
{
	HAL_Delay(100);

	SSD1306_device_t* init_dev = (SSD1306_device_t*)calloc(1, sizeof(SSD1306_device_t));

	if(init_dev == NULL) return NULL;

	//functions
	init_dev->command = &ssd1306_write_command;
	init_dev->clear = &ssd1306_clear;
	init_dev->update = &ssd1306_update_screen;
	init_dev->fill = &ssd1306_fill;
	init_dev->string = &ssd1306_write_string;
	init_dev->cursor = &ssd1306_set_cursor;

	init_dev->port = init_dev_vals->port;

	init_dev->width = init_dev_vals->width;
	init_dev->height = init_dev_vals->height;

	init_dev->background = init_dev_vals->background;
	init_dev->font = init_dev_vals->font;

	/* # Init LCD (chuỗi lệnh chuẩn SSD1306) */
	ssd1306_write_command(init_dev, 0xAE); //display off
	ssd1306_write_command(init_dev, 0x20); //memory addressing mode
	ssd1306_write_command(init_dev, 0x10); //00,Horizontal Addressing Mode;01,Vertical Addressing Mode;10,Page Addressing Mode (RESET);11,Invalid
	ssd1306_write_command(init_dev, 0xB0); //Set Page Start Address for Page Addressing Mode,0-7
	ssd1306_write_command(init_dev, 0xC8); //Set COM Output Scan Direction
	ssd1306_write_command(init_dev, 0x00); //---set low column address
	ssd1306_write_command(init_dev, 0x10); //---set high column address
	ssd1306_write_command(init_dev, 0x40); //--set start line address
	ssd1306_write_command(init_dev, 0x81); //--set contrast control register
	ssd1306_write_command(init_dev, 0xFF);
	ssd1306_write_command(init_dev, 0xA1); //--set segment re-map 0 to 127
	ssd1306_write_command(init_dev, 0xA6); //--set normal display
	ssd1306_write_command(init_dev, 0xA8); //--set multiplex ratio(1 to 64)
	ssd1306_write_command(init_dev, 0x3F); //
	ssd1306_write_command(init_dev, 0xA4); //0xa4,Output follows RAM content;0xa5,Output ignores RAM content
	ssd1306_write_command(init_dev, 0xD3); //-set display offset
	ssd1306_write_command(init_dev, 0x00); //-not offset
	ssd1306_write_command(init_dev, 0xD5); //--set display clock divide ratio/oscillator frequency
	ssd1306_write_command(init_dev, 0xF0); //--set divide ratio
	ssd1306_write_command(init_dev, 0xD9); //--set pre-charge period
	ssd1306_write_command(init_dev, 0x22); //
	ssd1306_write_command(init_dev, 0xDA); //--set com pins hardware configuration
	ssd1306_write_command(init_dev, 0x12);
	ssd1306_write_command(init_dev, 0xDB); //--set vcomh
	ssd1306_write_command(init_dev, 0x20); //0x20,0.77xVcc
	ssd1306_write_command(init_dev, 0x8D); //--set DC-DC enable
	ssd1306_write_command(init_dev, 0x14); //
	ssd1306_write_command(init_dev, 0xAF); //--turn on SSD1306 panel

	ssd1306_fill(init_dev, init_dev->background);

	ssd1306_update_screen(init_dev);

	init_dev->x = 0;
	init_dev->y = 0;

	init_dev->initialized = 1;

	return init_dev;
}

// # Gắn handle để dùng API tiện lợi không cần truyền self mỗi lần
void ssd1306_easy_attach(SSD1306_device_t* self)
{
	g_oled_easy = self;
}

// # Đặt cursor theo kiểu dễ dùng
void ssd1306_easy_set_cursor(uint8_t x, uint8_t y)
{
	if (g_oled_easy == NULL) return;
	ssd1306_set_cursor(g_oled_easy, x, y);
}

// # In chuỗi và update màn hình ngay
HAL_StatusTypeDef ssd1306_easy_print(const char* str)
{
	if ((g_oled_easy == NULL) || (str == NULL)) return HAL_ERROR;

	if (ssd1306_write_string(g_oled_easy, (char*)str) != HAL_OK)
		return HAL_ERROR;

	return ssd1306_update_screen(g_oled_easy);
}

// # In format kiểu printf: "Lux: %u", value
HAL_StatusTypeDef ssd1306_easy_printf(const char* fmt, ...)
{
	char buf[64];
	int n;
	va_list args;

	if ((g_oled_easy == NULL) || (fmt == NULL)) return HAL_ERROR;

	va_start(args, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (n < 0) return HAL_ERROR;

	return ssd1306_easy_print(buf);
}
