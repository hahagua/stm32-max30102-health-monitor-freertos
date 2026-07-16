/**
  ******************************************************************************
  * @file    app_rtos.c
  * @brief   MAX30102 心率血氧仪 - FreeRTOS 多任务版本
  * @note    本文件替代 main.c 参与 FreeRTOS 编译.
  *          原始 main.c 保持不动, 作为裸机版本保留.
  *          在 Keil 工程中: FreeRTOS 目标 → 包含 app_rtos.c, 排除 main.c
  ******************************************************************************
  */
#include "stm32f10x.h"
#include "Delay.h"
#include "LED.h"
#include "sys.h"
#include "usart.h"
#include "max30102.h"
#include "myi2c.h"
#include "OLED.h"
#include "alth_RF.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* =================================================================== */
/* 采样参数 (与原始 main.c 一致)                                        */
/* =================================================================== */
#define BUFFER_SIZE   ((uint8_t)(FS * ST))   /* FS=25, ST=4 → 100 */

/* =================================================================== */
/* 系统时钟初始化 (复制自原始 main.c: HSI→HSI/2×9 = 36MHz)             */
/* 启动文件已调用库版 SystemInit() (尝试 HSE 72MHz),                    */
/* 此处用本地版本覆盖为 36MHz, 与用户原始配置一致.                      */
/* =================================================================== */
static void SystemClock_Config(void)
{
    RCC->CR   |= (uint32_t)0x00000001;                 /* 使能 HSI */
    RCC->CFGR |= (uint32_t)RCC_CFGR_PLLSRC_HSI_Div2;   /* PLL 源 = HSI/2 */
    RCC->CFGR |= (uint32_t)RCC_CFGR_PLLMULL9;          /* ×9 → 4M×9=36MHz */
    RCC->CFGR |= (uint32_t)RCC_CFGR_HPRE_DIV1;         /* HCLK 不分频 */
    RCC->CR   |= RCC_CR_PLLON;                         /* 使能 PLL */
    while ((RCC->CR & RCC_CR_PLLRDY) == 0);            /* 等待锁定 */

    RCC->CFGR &= (uint32_t)(~(RCC_CFGR_SW));
    RCC->CFGR |= (uint32_t)RCC_CFGR_SW_PLL;            /* 选择 PLL 为系统时钟 */
    while ((RCC->CFGR & RCC_CFGR_SWS) != (uint32_t)0x08);

    /* 更新全局时钟变量 (Delay_us 的 DWT 依赖此值) */
    SystemCoreClock = 36000000;
}

/* =================================================================== */
/* FreeRTOS 内核对象                                                     */
/* =================================================================== */
static QueueHandle_t xSensorQueue = NULL;
static SemaphoreHandle_t xI2CMutex = NULL;

/* =================================================================== */
/* 传感器采集任务                                                        */
/* =================================================================== */
typedef struct {
    uint32_t ir[BUFFER_SIZE];
    uint32_t red[BUFFER_SIZE];
} SensorData_t;

static void vSensorTask(void *pvParameters)
{
    uint8_t uch_dummy;
    uint8_t temp[6];
    SensorData_t xData;
    int32_t  n;

    (void)pvParameters;

    /* --- 传感器初始化 (需要 I2C 总线) --- */
    xSemaphoreTake(xI2CMutex, portMAX_DELAY);
    max30102_reset();
    xSemaphoreGive(xI2CMutex);

    vTaskDelay(pdMS_TO_TICKS(1000));   /* 上电稳定 */

    xSemaphoreTake(xI2CMutex, portMAX_DELAY);
    maxim_max30102_read_reg(REG_INTR_STATUS_1, &uch_dummy);
    max30102_init();
    uch_dummy = max30102_Bus_Read(REG_PART_ID);
    xSemaphoreGive(xI2CMutex);

    OLED_ShowHexNum(1, 1, uch_dummy, 4);
    printf("RTOS| ID=0x%02X\r\n", uch_dummy);

    /* --- 主采集循环 --- */
    while (1)
    {
        xSemaphoreTake(xI2CMutex, portMAX_DELAY);   /* 锁定 I2C */

        for (n = 0; n < BUFFER_SIZE; n++)
        {
            /* 40ms 阻塞延时 → 25sps 采样率 */
            xSemaphoreGive(xI2CMutex);              /* 释放 I2C, 让其他任务使用 */
            vTaskDelay(pdMS_TO_TICKS(40));
            xSemaphoreTake(xI2CMutex, portMAX_DELAY);

            max30102_FIFO_ReadBytes(REG_FIFO_DATA, temp);

            xData.red[n] = ((uint32_t)(temp[0] & 0x03) << 16)
                         | ((uint32_t)temp[1] << 8)
                         |  (uint32_t)temp[2];

            xData.ir[n]  = ((uint32_t)(temp[3] & 0x03) << 16)
                         | ((uint32_t)temp[4] << 8)
                         |  (uint32_t)temp[5];
        }

        xSemaphoreGive(xI2CMutex);                 /* 释放 I2C */
        xQueueSend(xSensorQueue, &xData, portMAX_DELAY);  /* 交给算法任务 */
    }
}

/* =================================================================== */
/* 算法处理任务                                                          */
/* =================================================================== */
static void vAlgoTask(void *pvParameters)
{
    SensorData_t xData;
    float    n_spo2, ratio, correl;
    int8_t   ch_spo2_valid;
    int32_t  n_heart_rate;
    int8_t   ch_hr_valid;

    (void)pvParameters;

    while (1)
    {
        /* 阻塞等待传感器数据 */
        if (xQueueReceive(xSensorQueue, &xData, portMAX_DELAY) == pdPASS)
        {
            rf_heart_rate_and_oxygen_saturation(
                xData.ir, BUFFER_SIZE, xData.red,
                &n_spo2, &ch_spo2_valid,
                &n_heart_rate, &ch_hr_valid,
                &ratio, &correl);

            if (ch_hr_valid == 1 && ch_spo2_valid == 1)
            {
                printf("HR=%i, SpO2=%i\r\n",
                       (int)n_heart_rate, (int)n_spo2);

                OLED_ShowString(2, 1, "HR:");
                OLED_ShowNum(2, 4, n_heart_rate, 3);
                OLED_ShowString(3, 1, "SpO2:");
                OLED_ShowNum(3, 6, (uint16_t)n_spo2, 3);
                OLED_ShowString(3, 9, "%");
            }
        }
    }
}

/* =================================================================== */
/* 钩子函数                                                              */
/* =================================================================== */
void vApplicationMallocFailedHook_impl(void)
{
    taskDISABLE_INTERRUPTS();
    while (1);
}

void vApplicationStackOverflowHook_impl(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask; (void)pcTaskName;
    taskDISABLE_INTERRUPTS();
    while (1);
}

/* =================================================================== */
/* FreeRTOS 入口                                                         */
/* =================================================================== */
int main(void)
{
    /* 1. 系统时钟 (必须在所有外设初始化前) */
    SystemClock_Config();

    /* 2. NVIC 优先级分组 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* 3. 硬件初始化 */
    OLED_Init();
    Delay_init();            /* DWT 周期计数器 */
    uart_init(115200);

    printf("\r\n=== MAX30102 + FreeRTOS ===\r\n");

    /* 4. 创建内核对象 */
    xSensorQueue = xQueueCreate(1, sizeof(SensorData_t));
    xI2CMutex    = xSemaphoreCreateMutex();

    if (xSensorQueue == NULL || xI2CMutex == NULL)
    {
        printf("FATAL: kernel object create failed\r\n");
        while (1);
    }

    /* 5. 创建任务 */
    xTaskCreate(vSensorTask, "Sensor", 256, NULL, tskIDLE_PRIORITY + 3, NULL);
    xTaskCreate(vAlgoTask,   "Algo",   512, NULL, tskIDLE_PRIORITY + 2, NULL);

    /* 6. 启动调度器 */
    printf("Starting scheduler...\r\n");
    vTaskStartScheduler();

    while (1);
}
