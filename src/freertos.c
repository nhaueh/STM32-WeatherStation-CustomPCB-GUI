/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>

#include "ssd1306.h"
#include "adc.h"
#include "BH1750.h"
#include "hc06.h"
#include "bme280.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
volatile uint16_t g_pa4_raw = 0;
volatile uint32_t g_pa4_mv = 0;
volatile uint16_t g_lux = 0;
volatile BME280_Telemetry_t g_bme280_data;

/* USER CODE END Variables */
osThreadId defaultTaskHandle;
osThreadId oledtaskHandle;
osThreadId sensorTaskHandle;
osThreadId BluetoothtaskHandle;
osThreadId wifitaskHandle;
osThreadId idleTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void const * argument);
void oled(void const * argument);
void sensor(void const * argument);
void bluetooth(void const * argument);
void wifi(void const * argument);
void idle(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityLow, 0, 256);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* definition and creation of oledtask */
  osThreadDef(oledtask, oled, osPriorityBelowNormal, 0, 512);
  oledtaskHandle = osThreadCreate(osThread(oledtask), NULL);

  /* definition and creation of sensorTask */
  osThreadDef(sensorTask, sensor, osPriorityNormal, 0, 512);
  sensorTaskHandle = osThreadCreate(osThread(sensorTask), NULL);

  /* definition and creation of Bluetoothtask */
  osThreadDef(Bluetoothtask, bluetooth, osPriorityLow, 0, 256);
  BluetoothtaskHandle = osThreadCreate(osThread(Bluetoothtask), NULL);

  /* definition and creation of wifitask */
  osThreadDef(wifitask, wifi, osPriorityLow, 0, 384);
  wifitaskHandle = osThreadCreate(osThread(wifitask), NULL);

  /* definition and creation of idleTask */
  osThreadDef(idleTask, idle, osPriorityIdle, 0, 256);
  idleTaskHandle = osThreadCreate(osThread(idleTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_oled */
/**
* @brief Function implementing the oledtask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_oled */
void oled(void const * argument)
{
  /* USER CODE BEGIN oled */
  // # Lấy handle OLED từ main
  extern SSD1306_device_t* lcd;
  extern I2C_Safe_Bus_t g_i2c1_safe_bus;

  if (lcd == NULL)
  {
    SSD1306_device_init_t oled_init = {
      .background = Black,
      .font = &Font_7x10,
      .width = 128,
      .height = 64,
      .safe_bus = &g_i2c1_safe_bus,
      .port = g_i2c1_safe_bus.handle
    };

    while (lcd == NULL)
    {
      lcd = ssd1306_init(&oled_init);
      if (lcd == NULL)
      {
        osDelay(200);
      }
    }
  }

  ssd1306_easy_attach(lcd);
  ssd1306_easy_set_auto_update(0);
  
  /* Infinite loop */
  for(;;)
  {
    (void)ssd1306_fill(lcd, lcd->background);


    ssd1306_easy_set_cursor(0, 0);
    (void)ssd1306_easy_printf("Temp: %.1f C", g_bme280_data.temperature);

    ssd1306_easy_set_cursor(0, 12);
    (void)ssd1306_easy_printf("Pres: %.1f Pa", g_bme280_data.pressure);

    ssd1306_easy_set_cursor(0, 24);
    (void)ssd1306_easy_printf("Lux: %u", (unsigned)g_lux);

    ssd1306_easy_set_cursor(0, 36);
    (void)ssd1306_easy_printf("ADC: %lu mV", g_pa4_mv);

    (void)ssd1306_easy_flush();

    osDelay(300);
  }
  /* USER CODE END oled */
}

/* USER CODE BEGIN Header_sensor */
/**
* @brief Function implementing the sensorTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_sensor */
void sensor(void const * argument)
{
  /* USER CODE BEGIN sensor */
  extern I2C_Safe_Bus_t g_i2c1_safe_bus;
  uint8_t bh_poll_div = 0U;

  (void)BH1750_easy_init(&g_i2c1_safe_bus, true);

  /* Infinite loop */
  for(;;)
  {
    uint16_t raw = adc_easy_read_pa4();
    g_pa4_raw = raw;
    g_pa4_mv = ((uint32_t)raw * 3300UL) / 4095UL;

    g_bme280_data = bme280_easy_read(&g_i2c1_safe_bus);

    bh_poll_div++;
    if (bh_poll_div >= 3U)
    {
      bh_poll_div = 0U;

      g_lux = BH1750_easy_read_lux_or_zero();
    }

    osDelay(300);
  }
  /* USER CODE END sensor */
}

/* USER CODE BEGIN Header_bluetooth */
/**
* @brief Function implementing the Bluetoothtask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_bluetooth */
void bluetooth(void const * argument)
{
  /* USER CODE BEGIN bluetooth */
  extern UART_HandleTypeDef huart1;
  char cmd[32];
  GPIO_PinState pb13_state = GPIO_PIN_SET;
  GPIO_PinState pb14_state = GPIO_PIN_SET;
  GPIO_PinState pb15_state = GPIO_PIN_SET;
  GPIO_PinState pc13_state = GPIO_PIN_SET;
  uint16_t tx_period_ms = 0U;

  hc06_easy_attach(&huart1);
  (void)hc06_easy_print("BT ready. Send 1=ON, 2=OFF, PB13_TOGGLE/PB14_TOGGLE/PB15_TOGGLE/PC13_TOGGLE\r\n");

  /* Infinite loop */
  for(;;)
  {
    if (hc06_easy_read_line(cmd, sizeof(cmd)) > 0)
    {
      if (strcmp(cmd, "1") == 0)
      {
        pb15_state = GPIO_PIN_RESET;
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, pb15_state);
        (void)hc06_easy_print("PB15 ON\r\n");
      }
      else if (strcmp(cmd, "2") == 0)
      {
        pb15_state = GPIO_PIN_SET;
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, pb15_state);
        (void)hc06_easy_print("PB15 OFF\r\n");
      }
      else if (strcmp(cmd, "PB13_TOGGLE") == 0)
      {
        pb13_state = (pb13_state == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, pb13_state);
        (void)hc06_easy_print((pb13_state == GPIO_PIN_RESET) ? "PB13 ON\r\n" : "PB13 OFF\r\n");
      }
      else if (strcmp(cmd, "PB14_TOGGLE") == 0)
      {
        pb14_state = (pb14_state == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, pb14_state);
        (void)hc06_easy_print((pb14_state == GPIO_PIN_RESET) ? "PB14 ON\r\n" : "PB14 OFF\r\n");
      }
      else if (strcmp(cmd, "PB15_TOGGLE") == 0)
      {
        pb15_state = (pb15_state == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, pb15_state);
        (void)hc06_easy_print((pb15_state == GPIO_PIN_RESET) ? "PB15 ON\r\n" : "PB15 OFF\r\n");
      }
      else if (strcmp(cmd, "PC13_TOGGLE") == 0)
      {
        pc13_state = (pc13_state == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, pc13_state);
        (void)hc06_easy_print((pc13_state == GPIO_PIN_RESET) ? "PC13 ON\r\n" : "PC13 OFF\r\n");
      }
      else
      {
        (void)hc06_easy_print("Unknown cmd\r\n");
      }
    }

    tx_period_ms += 2U;
    if (tx_period_ms >= 1000U)
    {
      // Send ADC data in old format
      (void)hc06_easy_send_sensor(g_pa4_raw, g_pa4_mv, g_lux);
      
      // Also send Temperature and Pressure in new format
      char temp_pres_str[64];
      snprintf(temp_pres_str, sizeof(temp_pres_str), "Temp: %.1f C\r\nPres: %.1f Pa\r\n", 
               g_bme280_data.temperature, g_bme280_data.pressure);
      (void)hc06_easy_print(temp_pres_str);
      
      tx_period_ms = 0U;
    }

    osDelay(2);
  }
  /* USER CODE END bluetooth */
}

/* USER CODE BEGIN Header_wifi */
/**
* @brief Function implementing the wifitask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_wifi */
void wifi(void const * argument)
{
  /* USER CODE BEGIN wifi */
  extern UART_HandleTypeDef huart2;

  char *cmd1 = "AT\r\n";
  char *cmd2 = "AT+CWMODE=1\r\n";
  char *cmd3 = "AT+CWJAP=\"Minh Huyen\",\"Huyen1358\"\r\n";

  /* Infinite loop */
  for(;;)
  {
    HAL_UART_Transmit(&huart2, (uint8_t *)cmd1, strlen(cmd1), 100);
    osDelay(2000);

    HAL_UART_Transmit(&huart2, (uint8_t *)cmd2, strlen(cmd2), 100);
    osDelay(2000);

    HAL_UART_Transmit(&huart2, (uint8_t *)cmd3, strlen(cmd3), 100);
    osDelay(5000);
  }
  /* USER CODE END wifi */
}

/* USER CODE BEGIN Header_idle */
/**
* @brief Function implementing the idleTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_idle */
void idle(void const * argument)
{
  /* USER CODE BEGIN idle */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END idle */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  hc06_easy_rx_irq_callback(huart);
}

/* USER CODE END Application */

