#include "hc06.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define HC06_RX_BUF_SIZE 128U
#define HC06_TX_BUF_SIZE 128U

static UART_HandleTypeDef* g_hc06_uart = NULL;
static uint8_t g_irq_rx_byte = 0U;

static volatile uint16_t g_rx_head = 0U;
static volatile uint16_t g_rx_tail = 0U;
static uint8_t g_rx_buf[HC06_RX_BUF_SIZE];

static uint16_t hc06_next_index(uint16_t idx)
{
    return (uint16_t)((idx + 1U) % HC06_RX_BUF_SIZE);
}

static void hc06_push_byte(uint8_t b)
{
    uint16_t next = hc06_next_index(g_rx_head);

    if (next == g_rx_tail)
    {
        g_rx_tail = hc06_next_index(g_rx_tail);
    }

    g_rx_buf[g_rx_head] = b;
    g_rx_head = next;
}

static int hc06_pop_byte(uint8_t* out)
{
    if ((out == NULL) || (g_rx_tail == g_rx_head))
    {
        return 0;
    }

    *out = g_rx_buf[g_rx_tail];
    g_rx_tail = hc06_next_index(g_rx_tail);
    return 1;
}

void hc06_easy_attach(UART_HandleTypeDef* uart)
{
    g_hc06_uart = uart;
    g_rx_head = 0U;
    g_rx_tail = 0U;

    if (g_hc06_uart != NULL)
    {
        (void)HAL_UART_Receive_IT(g_hc06_uart, &g_irq_rx_byte, 1U);
    }
}

void hc06_easy_rx_irq_callback(UART_HandleTypeDef* huart)
{
    if ((g_hc06_uart == NULL) || (huart->Instance != g_hc06_uart->Instance))
    {
        return;
    }

    hc06_push_byte(g_irq_rx_byte);
    (void)HAL_UART_Receive_IT(g_hc06_uart, &g_irq_rx_byte, 1U);
}

int hc06_easy_read_line(char* out, uint16_t out_size)
{
    static char line[HC06_LINE_MAX_LEN];
    static uint16_t line_len = 0U;
    uint8_t ch;

    if ((out == NULL) || (out_size < 2U))
    {
        return -1;
    }

    while (hc06_pop_byte(&ch) != 0)
    {
        if ((ch == '\r') || (ch == '\n'))
        {
            if (line_len == 0U)
            {
                continue;
            }

            if (line_len >= out_size)
            {
                line_len = (uint16_t)(out_size - 1U);
            }

            memcpy(out, line, line_len);
            out[line_len] = '\0';

            {
                int ret = (int)line_len;
                line_len = 0U;
                return ret;
            }
        }

        if (line_len < (uint16_t)(HC06_LINE_MAX_LEN - 1U))
        {
            line[line_len++] = (char)ch;
        }
        else
        {
            line_len = 0U;
        }
    }

    return 0;
}

HAL_StatusTypeDef hc06_easy_print(const char* str)
{
    if ((g_hc06_uart == NULL) || (str == NULL))
    {
        return HAL_ERROR;
    }

    return HAL_UART_Transmit(g_hc06_uart, (uint8_t*)str, (uint16_t)strlen(str), 100U);
}

HAL_StatusTypeDef hc06_easy_printf(const char* fmt, ...)
{
    char buf[HC06_TX_BUF_SIZE];
    int n;
    va_list ap;

    if ((g_hc06_uart == NULL) || (fmt == NULL))
    {
        return HAL_ERROR;
    }

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n <= 0)
    {
        return HAL_ERROR;
    }

    if ((size_t)n >= sizeof(buf))
    {
        n = (int)(sizeof(buf) - 1U);
    }

    return HAL_UART_Transmit(g_hc06_uart, (uint8_t*)buf, (uint16_t)n, 100U);
}

HAL_StatusTypeDef hc06_easy_send_sensor(uint16_t raw, uint32_t mv, uint16_t lux)
{
    return hc06_easy_printf("RAW=%u mV=%lu LUX=%u\r\n",
                            (unsigned)raw,
                            (unsigned long)mv,
                            (unsigned)lux);
}
