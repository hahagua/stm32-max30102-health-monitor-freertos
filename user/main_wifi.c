/**
 ******************************************************************************
 * @file    main_wifi.c
 * @brief   WiFi 版 — MAX30102 + ESP8266 透传
 *
 * Keil: User 组加入 main_wifi.c, 排除 main_minimal.c
 *       Hardware 编译 esp_simple.c, MPU6050.c, I2C.c
 *       Hardware 排除 esp8266.c, LED.c
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
#include "esp_simple.h"
#include "MPU6050.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define BUFFER_SIZE       ((uint8_t)(FS * ST))
#define SENSOR_STACK      384
#define ALGO_STACK        512
#define TX_STACK           256

#define WIFI_SSID  "YOUR_WIFI_SSID"
#define WIFI_PASS  "YOUR_WIFI_PASSWORD"
#define PC_IP      "YOUR_PC_IP_ADDRESS"

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

static void proto_send(const uint8_t *frame, uint8_t len)
{
    uint8_t i;
    for (i = 0; i < len; i++) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
        USART_SendData(USART1, frame[i]);
    }
}

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

static void vTxTask(void *pvParameters)
{
    HRResult_t hr;
    uint8_t    seq = 0;
    uint8_t    frame[FRAME_BUF_SIZE];
    uint8_t    len;
    TickType_t xLastWakeTime;
    /* 运动检测状态 (内嵌, 省堆) */
    int16_t    mpu_ax, mpu_ay, mpu_az, mpu_gx, mpu_gy, mpu_gz;
    uint32_t   mag_prev = 0;
    uint8_t    mpu_inited = 0;
    uint32_t   delta_sum;
    uint16_t   score;
    uint8_t    level;
    (void)pvParameters;

    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        /* 心率 (非阻塞) */
        if (xQueueReceive(xHRQueue, &hr, 0) == pdPASS) {
            len = proto_pack_heart_rate(
                seq, hr.hr, hr.spo2_x10, hr.hr_valid, hr.spo2_valid, frame);
            proto_send(frame, len);
            esp_simple_send(frame, len);
            seq++;
        }

        /* MPU6050 运动检测 (1Hz) */
        mpu6050_getdata(&mpu_ax, &mpu_ay, &mpu_az, &mpu_gx, &mpu_gy, &mpu_gz);
        {
            uint32_t m = 0;
            if (mpu_ax < 0) m -= mpu_ax; else m += mpu_ax;
            if (mpu_ay < 0) m -= mpu_ay; else m += mpu_ay;
            if (mpu_az < 0) m -= mpu_az; else m += mpu_az;
            if (mpu_inited) {
                delta_sum = (m > mag_prev) ? (m - mag_prev) : (mag_prev - m);
                if      (delta_sum < 500)   level = 0;
                else if (delta_sum < 2000)  level = 1;
                else if (delta_sum < 6000)  level = 2;
                else                        level = 3;
                score = (delta_sum > 1023) ? 1023 : (uint16_t)delta_sum;
                len = proto_pack_motion(seq, score, level, frame);
                proto_send(frame, len);
                esp_simple_send(frame, len);
                seq++;
                /* 同时发原始 6 轴数据给上位机画波形 */
                len = proto_pack_mpu6050(seq, mpu_ax, mpu_ay, mpu_az,
                                         mpu_gx, mpu_gy, mpu_gz, frame);
                proto_send(frame, len);
                esp_simple_send(frame, len);
                seq++;
            } else {
                mpu_inited = 1;
            }
            mag_prev = m;
        }

        /* 心跳帧 */
        vTaskDelay(pdMS_TO_TICKS(500));
        len = proto_pack_heartbeat(seq, frame);
        proto_send(frame, len);
        esp_simple_send(frame, len);
        seq++;

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}

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

int main(void)
{
    SystemClock_Config();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    Delay_init();
    uart_init(115200);
    uart_printf_mutex_init();
    OLED_Init();

    printf("\r\n=== MAX RTOS (WiFi) ===\r\n");

    printf("[WiFi] 初始化...\r\n");
    esp_simple_init();
    esp_simple_connect(WIFI_SSID, WIFI_PASS, PC_IP, 8888);

    /* MPU6050 初始化 */
    printf("[MPU] 初始化...\r\n");
    if (mpu_Init() == 0)
        printf("[MPU] OK\r\n");
    else
        printf("[MPU] FAIL (WHO_AM_I 不匹配)\r\n");

    xI2CMutex    = xSemaphoreCreateMutex();
    xOLEDMutex   = xSemaphoreCreateMutex();
    xSensorQueue = xQueueCreate(1, sizeof(SensorData_t));
    xHRQueue     = xQueueCreate(1, sizeof(HRResult_t));

    xTaskCreate(vSensorTask,  "Sens", SENSOR_STACK, NULL, tskIDLE_PRIORITY + 4, NULL);
    xTaskCreate(vAlgoTask,    "Algo", ALGO_STACK,   NULL, tskIDLE_PRIORITY + 3, NULL);
    xTaskCreate(vTxTask,      "Tx",   TX_STACK,     NULL, tskIDLE_PRIORITY + 2, NULL);

    printf("Tasks: Sens Algo Tx\r\n");
    printf("---\r\n");

    vTaskStartScheduler();
    while (1);
}
