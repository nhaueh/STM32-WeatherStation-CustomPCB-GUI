#ifndef __HC06_H__
#define __HC06_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "usart.h"

#define HC06_LINE_MAX_LEN 64U

void hc06_easy_attach(UART_HandleTypeDef* uart);
void hc06_easy_rx_irq_callback(UART_HandleTypeDef* huart);

int hc06_easy_read_line(char* out, uint16_t out_size);

HAL_StatusTypeDef hc06_easy_print(const char* str);
HAL_StatusTypeDef hc06_easy_printf(const char* fmt, ...);
HAL_StatusTypeDef hc06_easy_send_sensor(uint16_t raw, uint32_t mv, uint16_t lux);

#ifdef __cplusplus
}
#endif

#endif /* __HC06_H__ */
