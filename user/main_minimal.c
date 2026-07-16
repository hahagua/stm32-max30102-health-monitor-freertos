/**
 ******************************************************************************
 * @file    main_minimal.c
 * @brief   精简版 — MAX30102 心率血氧 + 二进制协议帧 → USART1
 *
 * 不依赖 MPU6050, 快速验证上位机联调链路。
 *
 * 在 Keil 中: 排除 main.c / app_rtos.c / main_mpu.c, 加入本文件
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "Delay.h"
#include "sys.h"
#include "usart.h"
#include "max30102.h"
#include "myi2c.h"
#include "OLED.h"
#include "alth_RF.h"
#include "protocol.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* =================================================================== */
/* 配置                                                                  */
/* =================================================================== */
#define BUFFER_SIZE       ((uint8_t)(FS * ST))

#define SENSOR_STACK      384
#define ALGO_STACK        512
#define TX_STACK           256

/* =================================================================== */
/* 队列 + 信号量                                                          */
/* =================================================================== */

typedef struct {
    uint32_t ir[BUFFER_SIZE];
    uint32_t red[BUFFER_SIZE];
} SensorData_t;

typedef struct {
    uint16_t hr;
    uint16_t spo2_x10;
    uint8_t  hr_valid;
    uint8_t  spo2_valid;
} HRResult_t;

static QueueHandle_t xSensorQueue = NULL;
static QueueHandle_t xHRQueue     = NULL;
static SemaphoreHandle_t xI2CMutex  = NULL;
static SemaphoreHandle_t xOLEDMutex = NULL;

/* =================================================================== */
/* 系统时钟                                                               */
/* =================================================================== */

static void SystemClock_Config(void)
{
    RCC->CFGR &= ~(RCC_CFGR_SW);
    RCC->CFGR |= RCC_CFGR_SW_HSI;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI);
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY) {}
    RCC->CFGR &= ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL);
    RCC->CFGR |= RCC_CFGR_PLLSRC_HSI_Div2;
    RCC->CFGR |= RCC_CFGR_PLLMULL9;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0);
    FLASH->ACR &= ~FLASH_ACR_LATENCY;
    FLASH->ACR |= FLASH_ACR_LATENCY_1;
    RCC->CFGR &= ~(RCC_CFGR_SW);
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
    SystemCoreClock = 36000000;
}

/* =================================================================== */
/* USART1 二进制发送                                                       */
/* =================================================================== */

static void proto_send(const uint8_t *frame, uint8_t len)
{
    uint8_t i;
    for (i = 0; i < len; i++) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
        USART_SendData(USART1, frame[i]);
    }
}

/* =================================================================== */
/* vSensorTask — MAX30102 采集                                           */
/* =================================================================== */

static void vSensorTask(void *pvParameters)
{
    uint8_t uch_dummy, temp[6];
    SensorData_t xData;
    int32_t n;
    (void)pvParameters;

    xSemaphoreTake(xI2CMutex, portMAX_DELAY);
    max30102_reset();
    xSemaphoreGive(xI2CMutex);
    vTaskDelay(pdMS_TO_TICKS(1000));

    xSemaphoreTake(xI2CMutex, portMAX_DELAY);
    maxim_max30102_read_reg(REG_INTR_STATUS_1, &uch_dummy);
    max30102_init();
    uch_dummy = max30102_Bus_Read(REG_PART_ID);
    xSemaphoreGive(xI2CMutex);

    xSemaphoreTake(xOLEDMutex, portMAX_DELAY);
    OLED_ShowHexNum(1, 1, uch_dummy, 4);
    xSemaphoreGive(xOLEDMutex);

    while (1) {
        xSemaphoreTake(xI2CMutex, portMAX_DELAY);
        for (n = 0; n < BUFFER_SIZE; n++) {
            xSemaphoreGive(xI2CMutex);
            vTaskDelay(pdMS_TO_TICKS(40));
            xSemaphoreTake(xI2CMutex, portMAX_DELAY);
            max30102_FIFO_ReadBytes(REG_FIFO_DATA, temp);
            xData.red[n] = ((uint32_t)(temp[0] & 0x03) << 16)
                         | ((uint32_t)temp[1] << 8) | temp[2];
            xData.ir[n]  = ((uint32_t)(temp[3] & 0x03) << 16)
                         | ((uint32_t)temp[4] << 8) | temp[5];
        }
        xSemaphoreGive(xI2CMutex);
        xQueueSend(xSensorQueue, &xData, portMAX_DELAY);
    }
}

/* =================================================================== */
/* vAlgoTask — 心率算法                                                    */
/* =================================================================== */

static void vAlgoTask(void *pvParameters)
{
    SensorData_t xData;
    HRResult_t   hr;
    float sp, ra, co;
    int8_t sv, hv;
    int32_t hr_val;
    (void)pvParameters;

    while (1) {
        if (xQueueReceive(xSensorQueue, &xData, portMAX_DELAY) == pdPASS) {
            rf_heart_rate_and_oxygen_saturation(
                xData.ir, BUFFER_SIZE, xData.red,
                &sp, &sv, &hr_val, &hv, &ra, &co);

            hr.hr         = (hv == 1) ? (uint16_t)hr_val : 0;
            hr.spo2_x10   = (sv == 1) ? (uint16_t)(sp * 10.0f) : 0;
            hr.hr_valid   = (hv == 1) ? 1 : 0;
            hr.spo2_valid = (sv == 1) ? 1 : 0;

            xQueueSend(xHRQueue, &hr, 0);

            if (hv == 1 && sv == 1) {
                /* OLED 显示 (printf 会干扰二进制帧, 已关闭) */
                xSemaphoreTake(xOLEDMutex, portMAX_DELAY);
                OLED_ShowString(2, 1, "HR:");
                OLED_ShowNum(2, 4, hr_val, 3);
                OLED_ShowString(3, 1, "SpO2:");
                OLED_ShowNum(3, 6, (uint16_t)sp, 3);
                OLED_ShowString(3, 9, "%");
                xSemaphoreGive(xOLEDMutex);
            }
        }
    }
}

/* =================================================================== */
/* vTxTask — 心率帧 + 心跳帧                                              */
/* =================================================================== */

static void vTxTask(void *pvParameters)
{
    HRResult_t hr;
    uint8_t    seq = 0;
    uint8_t    frame[FRAME_BUF_SIZE];
    uint8_t    len;
    TickType_t xLastWakeTime;
    (void)pvParameters;

    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        /* 心率 (非阻塞, 有新值才发) */
        if (xQueueReceive(xHRQueue, &hr, 0) == pdPASS) {
            len = proto_pack_heart_rate(
                seq, hr.hr, hr.spo2_x10, hr.hr_valid, hr.spo2_valid, frame);
            proto_send(frame, len);
            seq++;
        }

        /* 心跳帧 (每 1 秒) */
        len = proto_pack_heartbeat(seq, frame);
        proto_send(frame, len);
        seq++;

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}

/* =================================================================== */
/* 钩子                                                                    */
/* =================================================================== */

void vApplicationMallocFailedHook_impl(void)
{
    taskDISABLE_INTERRUPTS();
    while (1);
}

void vApplicationStackOverflowHook_impl(TaskHandle_t x, char *n)
{
    (void)x; (void)n;
    taskDISABLE_INTERRUPTS();
    while (1);
}

/* =================================================================== */
/* main                                                                   */
/* =================================================================== */

int main(void)
{
    SystemClock_Config();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    Delay_init();
    uart_init(115200);
    uart_printf_mutex_init();
    OLED_Init();

    printf("\r\n=== MAX RTOS 心率血氧仪 ===\r\n");

    xI2CMutex    = xSemaphoreCreateMutex();
    xOLEDMutex   = xSemaphoreCreateMutex();
    xSensorQueue = xQueueCreate(1, sizeof(SensorData_t));
    xHRQueue     = xQueueCreate(1, sizeof(HRResult_t));

    xTaskCreate(vSensorTask, "Sens",   SENSOR_STACK, NULL, tskIDLE_PRIORITY + 4, NULL);
    xTaskCreate(vAlgoTask,   "Algo",   ALGO_STACK,   NULL, tskIDLE_PRIORITY + 3, NULL);
    xTaskCreate(vTxTask,     "Tx",     TX_STACK,     NULL, tskIDLE_PRIORITY + 2, NULL);

    printf("Tasks: Sens Algo Tx\r\n");
    printf("Binary mode — no more text after this line.\r\n");
    printf("---\r\n");

    vTaskStartScheduler();
    while (1);
}
