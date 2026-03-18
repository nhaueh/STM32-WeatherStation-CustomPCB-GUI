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
#include "ssd1306.h"
#include "adc.h"
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
  osThreadDef(Bluetoothtask, bluetooth, osPriorityAboveNormal, 0, 256);
  BluetoothtaskHandle = osThreadCreate(osThread(Bluetoothtask), NULL);

  /* definition and creation of wifitask */
  osThreadDef(wifitask, wifi, osPriorityAboveNormal, 0, 384);
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

  ssd1306_easy_attach(lcd);
  
  /* Infinite loop */
  for(;;)
  {
    ssd1306_clear(lcd);

    ssd1306_easy_set_cursor(0, 0);
    ssd1306_easy_print("ADC1 IN4 (PA4)");

    ssd1306_easy_set_cursor(0, 16);
    ssd1306_easy_printf("RAW: %u", (unsigned)g_pa4_raw);

    ssd1306_easy_set_cursor(0, 32);
    ssd1306_easy_printf("mV : %lu", g_pa4_mv);

    osDelay(200);
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
  /* Infinite loop */
  for(;;)
  {
    uint16_t raw = adc_easy_read_pa4();
    g_pa4_raw = raw;
    g_pa4_mv = ((uint32_t)raw * 3300UL) / 4095UL;

    osDelay(100);
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
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
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
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
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

/* USER CODE END Application */

