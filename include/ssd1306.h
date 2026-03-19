/**
 * @author  Alexander Hoffman
 * @email   alxhoff@gmail.com
 * @website http://alexhoffman.info
 * @license GNU GPL v3
 * @brief   STM32 HAL library for LCD screens using the SSD1306 controller
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

// # TODO: Có thể refactor buffer thành con trỏ để linh hoạt kích thước màn hình.
// # TODO: Có thể bổ sung thêm các lệnh mở rộng theo datasheet SSD1306.

#include "stm32f1xx_hal.h"
#include "fonts.h"
#include "i2c_safe.h"

#ifndef ssd1306
#define ssd1306

// # Địa chỉ I2C 8-bit của SSD1306 (0x3C << 1 = 0x78)
#define SSD1306_I2C_ADDR        0x78

// # Cấu hình mặc định của driver
#define SSD1306_I2C_PORT		hi2c1
#define SSD1306_WIDTH           128
#define SSD1306_HEIGHT          64
#define SSD1306_BACKGROUND		0

// # Macro tiện ích nếu bạn gắn SSD1306 vào một struct cấp cao hơn (keyboard_devs).
#define GET_LCD keyboard_devs->LCD
#define LCD_CLEAR keyboard_devs->LCD->clear(keyboard_devs->LCD)
#define LCD_SET_CURSOR(x,y) keyboard_devs->LCD->cursor(keyboard_devs->LCD, x, y)
#define LCD_WRITE_STRING(input) keyboard_devs->LCD->string(keyboard_devs->LCD, input)
#define LCD_UPDATE keyboard_devs->LCD->update(keyboard_devs->LCD)

typedef enum {
	Black = 0x00, /*!< Black color, no pixel */
	White = 0x01  /*!< Pixel is set. Color depends on LCD */
} SSD1306_colour_t;

// # Struct thiết bị SSD1306:
// # - x,y: vị trí con trỏ hiện tại
// # - initialized: cờ đã init
// # - background: màu nền mặc định
// # - font: font đang dùng để viết text
// # - width,height: kích thước panel
// # - buffer: framebuffer 1-bit (W*H/8)
// # - safe_bus: wrapper I2C có mutex dùng chung cho FreeRTOS
// # - port: cổng I2C HAL (fallback nếu chưa dùng Safe I2C)
// # - function pointers: API dạng OOP-style trong C
typedef struct SSD1306_device SSD1306_device_t;
struct SSD1306_device{
	uint16_t x;
	uint16_t y;

	uint8_t initialized;

	SSD1306_colour_t background;

	FontDef* font;

	uint8_t width;
	uint8_t height;

	uint8_t buffer [SSD1306_WIDTH * SSD1306_HEIGHT / 8];

	I2C_Safe_Bus_t* safe_bus;
	I2C_HandleTypeDef* port;

	HAL_StatusTypeDef (*command)(SSD1306_device_t*, uint8_t);
	HAL_StatusTypeDef (*clear)(SSD1306_device_t*);
	HAL_StatusTypeDef (*update)(SSD1306_device_t*);
	HAL_StatusTypeDef (*fill)(SSD1306_device_t*, SSD1306_colour_t);
	HAL_StatusTypeDef (*string)(SSD1306_device_t*, char*);
	void (*cursor)(SSD1306_device_t*, uint8_t, uint8_t);
};

// # Tham số khởi tạo để truyền vào ssd1306_init()
typedef struct SSD1306_device_init{
	SSD1306_colour_t background;

	FontDef* font;

	uint8_t width;
	uint8_t height;

	I2C_Safe_Bus_t* safe_bus;
	I2C_HandleTypeDef* port;
}SSD1306_device_init_t;

// # API chính của thư viện
// # ssd1306_init: khởi tạo struct + gửi chuỗi lệnh init panel
SSD1306_device_t* ssd1306_init(SSD1306_device_init_t* init_dev_vals);

// # fill: chỉ fill buffer RAM (chưa đẩy lên màn hình)
HAL_StatusTypeDef ssd1306_fill(SSD1306_device_t* self, SSD1306_colour_t color);

// # update_screen: đẩy buffer ra OLED qua I2C theo từng page
HAL_StatusTypeDef ssd1306_update_screen(SSD1306_device_t* self);

// # clear: fill theo background + update ngay
HAL_StatusTypeDef ssd1306_clear(SSD1306_device_t* self);

// # write_command: gửi 1 byte command tới SSD1306
HAL_StatusTypeDef ssd1306_write_command(SSD1306_device_t* self, uint8_t command);

// # draw_pixel: set/clear 1 pixel trong buffer
HAL_StatusTypeDef ssd1306_draw_pixel(SSD1306_device_t* self,
		uint8_t x, uint8_t y, SSD1306_colour_t colour);

// # write_char: vẽ 1 ký tự bằng font đã chọn
HAL_StatusTypeDef ssd1306_write_char(SSD1306_device_t* self,
		char ch, FontDef Font, SSD1306_colour_t color);

// # write_string: vẽ chuỗi từ vị trí con trỏ hiện tại
HAL_StatusTypeDef ssd1306_write_string(SSD1306_device_t* self, char* str);

// # set_cursor: đặt vị trí con trỏ text
void ssd1306_set_cursor(SSD1306_device_t* self, uint8_t x, uint8_t y);

// # set_safe_bus: gắn/chuyển bus Safe I2C cho SSD1306 runtime.
// # Lưu ý: toàn bộ thiết bị I2C nên dùng cùng 1 mutex để tránh đụng bus.
void ssd1306_set_safe_bus(SSD1306_device_t* self, I2C_Safe_Bus_t* safe_bus);

// # API tiện dụng kiểu Arduino:
// # - attach: gắn handle màn hình dùng toàn cục
// # - set_cursor: đặt vị trí in
// # - print: in chuỗi và update luôn
// # - printf: in format (số, text) và update luôn
void ssd1306_easy_attach(SSD1306_device_t* self);
void ssd1306_easy_set_cursor(uint8_t x, uint8_t y);
HAL_StatusTypeDef ssd1306_easy_print(const char* str);
HAL_StatusTypeDef ssd1306_easy_printf(const char* fmt, ...);
void ssd1306_easy_set_auto_update(uint8_t enable);
HAL_StatusTypeDef ssd1306_easy_flush(void);

#endif
