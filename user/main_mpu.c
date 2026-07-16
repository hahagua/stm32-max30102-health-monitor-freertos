/**
 ******************************************************************************
 * @file    main_mpu.c
 * @brief   MAX RTOS 多传感器整合版 (MPU6050 + MAX30102 + 二进制协议)
 *
 * 数据流:
 *   MAX30102 → SensorTask → AlgoTask (心率/血氧) ──┐
 *                                                    ├→ TxTask → USART1
 *   MPU6050  → MpuTask  → MotionTask (运动分数) ───┘  (二进制帧)
 *
 * 输出: USART1 透传二进制协议帧, 上位机 FrameParser 自动同步。
 *       printf 调试文本可混用 — 协议状态机自动跳过非 0xAA 字节。
 *
 * 编译: 本文件替代 main.c 加入 Keil 工程。
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
#include "MPU6050.h"
#include "protocol.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* =================================================================== */
/* 配置常量                                                              */
/* =================================================================== */
#define BUFFER_SIZE       ((uint8_t)(FS * ST))  /* MAX30102: FS=25, ST=4 → 100 */

#define MPU_RATE_HZ       100                   /* MPU6050 采样率 */
#define MPU_INTERVAL_MS   (1000 / MPU_RATE_HZ)  /* 10 ms */

#define HR_INTERVAL_MS    4000                  /* 心率计算间隔 4s */
#define HB_INTERVAL_MS    1000                  /* 心跳间隔 1s */

/* 栈大小: SensorData_t=800B, frame_buf=261B 在栈上 */
#define SENSOR_STACK      384   /* 800B结构体 + 64B上下文 + 函数调用 */
#define ALGO_STACK        512   /* 浮点算法 + 800B结构体 */
#define MPU_STACK          224   /* frame_buf[261] + proto_send */
#define MOTION_STACK       256   /* float acc_sq[50]=200B */
#define TX_STACK           224   /* frame_buf[261] + proto_send */

/* =================================================================== */
/* 数据结构                                                              */
/* =================================================================== */

/* MAX30102 传感器原始数据 */
typedef struct {
    uint32_t ir[BUFFER_SIZE];
    uint32_t red[BUFFER_SIZE];
} SensorData_t;

/* 心率/血氧计算结果 */
typedef struct {
    uint16_t hr;
    uint16_t spo2_x10;   /* e.g. 985 = 98.5% */
    uint8_t  hr_valid;
    uint8_t  spo2_valid;
} HRResult_t;

/* MPU6050 原始数据 */
typedef struct {
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
} MpuData_t;

/* 运动分数 */
typedef struct {
    uint16_t score;      /* 0~1023 */
    uint8_t  level;      /* 0=静止,1=微动,2=步行,3=跑步,4=剧烈 */
} MotionResult_t;

/* =================================================================== */
/* FreeRTOS 内核对象                                                      */
/* =================================================================== */

static QueueHandle_t xSensorQueue = NULL;   /* MAX30102 raw → Algo   */
static QueueHandle_t xHRQueue     = NULL;   /* HR result → Tx        */
static QueueHandle_t xMpuQueue    = NULL;   /* MPU6050 raw → Motion  */
static QueueHandle_t xMotionQueue = NULL;   /* Motion result → Tx    */

static SemaphoreHandle_t xI2CMutex   = NULL;   /* 保护软件 I2C (MAX30102+OLED) */
static SemaphoreHandle_t xOLEDMutex  = NULL;

/* =================================================================== */
/* 系统时钟 (同步自 app_rtos.c)                                          */
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
/* 协议帧发送 (USART1 裸写, 绕过 printf)                                  */
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
/* MAX30102 传感器采集任务 (与原始 app_rtos.c 相同)                       */
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
    printf("MAX30102 ID=0x%02X\r\n", uch_dummy);

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
/* 心率/血氧算法任务                                                      */
/* =================================================================== */

static void vAlgoTask(void *pvParameters)
{
    SensorData_t xData;
    HRResult_t   hrResult;
    float sp, ra, co;
    int8_t sv, hv;
    int32_t hr_val;
    (void)pvParameters;

    while (1) {
        if (xQueueReceive(xSensorQueue, &xData, portMAX_DELAY) == pdPASS) {
            rf_heart_rate_and_oxygen_saturation(
                xData.ir, BUFFER_SIZE, xData.red,
                &sp, &sv, &hr_val, &hv, &ra, &co);

            hrResult.hr         = (hv == 1) ? (uint16_t)hr_val : 0;
            hrResult.spo2_x10   = (sv == 1) ? (uint16_t)(sp * 10.0f) : 0;
            hrResult.hr_valid   = (hv == 1) ? 1 : 0;
            hrResult.spo2_valid = (sv == 1) ? 1 : 0;

            xQueueSend(xHRQueue, &hrResult, 0);  /* 不阻塞, 丢旧值 */

            if (hv == 1 && sv == 1) {
                printf("HR=%i, SpO2=%i\r\n", (int)hr_val, (int)sp);
                xSemaphoreTake(xOLEDMutex, portMAX_DELAY);
                OLED_ShowString(2, 1, "HR:");  OLED_ShowNum(2, 4, hr_val, 3);
                OLED_ShowString(3, 1, "SpO2:"); OLED_ShowNum(3, 6, (uint16_t)sp, 3);
                OLED_ShowString(3, 9, "%");
                xSemaphoreGive(xOLEDMutex);
            }
        }
    }
}

/* =================================================================== */
/* MPU6050 采集任务 (100Hz)                                               */
/* =================================================================== */

static void vMpuTask(void *pvParameters)
{
    MpuData_t mpu;
    uint8_t   seq = 0;
    uint8_t   frame_buf[FRAME_BUF_SIZE];
    uint8_t   frame_len;
    TickType_t xLastWakeTime;
    (void)pvParameters;

    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        /* 精确周期: 10ms = 100Hz */
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(MPU_INTERVAL_MS));

        mpu6050_getdata(&mpu.ax, &mpu.ay, &mpu.az,
                        &mpu.gx, &mpu.gy, &mpu.gz);

        /* 发送到运动检测队列 */
        xQueueSend(xMpuQueue, &mpu, 0);

        /* 每 10 个采样 (100ms) 发一帧 MPU6050 数据 (节省带宽) */
        if ((seq % 10) == 0) {
            frame_len = proto_pack_mpu6050(
                seq, mpu.ax, mpu.ay, mpu.az, mpu.gx, mpu.gy, mpu.gz,
                frame_buf);
            proto_send(frame_buf, frame_len);
        }

        seq++;
    }
}

/* =================================================================== */
/* 运动检测任务 (10Hz)                                                    */
/* =================================================================== */

static void vMotionTask(void *pvParameters)
{
    MpuData_t     mpu;
    MotionResult_t mot;
    uint8_t       seq = 0;
    uint8_t       frame_buf[FRAME_BUF_SIZE];
    uint8_t       frame_len;

    /*
     * 运动检测算法 — 基于加速度平方幅值 (避免 sqrt/浮点库依赖)
     *
     * 原理: |a|^2 = ax^2 + ay^2 + az^2, 静止时 ≈ 1g^2
     * 动态分量 = | |a|^2 - 1g^2 |  (单位: g^2)
     *
     * LSB → g 转换 (16384 LSB/g → 除以 2^14):
     *   ax_g^2 ≈ (ax >> 14)^2  → 误差较大
     *   ax_g^2 = (ax * ax) / (16384 * 16384) = (ax * ax) >> 28
     *   但 float 更直观: ax_g = ax * (1.0f/16384.0f)
     */

    #define MOTION_WINDOW   50          /* 50 × 10ms = 0.5s 滑动窗 */
    #define GRAVITY_SQ      1.0f        /* 1g 对应的平方幅值 */
    float acc_sq[MOTION_WINDOW] = {0};  /* 环形窗: (|a|^2 - 1) */
    uint8_t sq_idx = 0;
    float sum_sq = 0.0f;                /* 窗内 sum */

    const float inv_lsb = 1.0f / 16384.0f;  /* ±2g → 16384 LSB/g */
    (void)pvParameters;

    while (1) {
        if (xQueueReceive(xMpuQueue, &mpu, pdMS_TO_TICKS(200)) == pdPASS) {
            /* 转换为 g 值 */
            float ax_g = (float)mpu.ax * inv_lsb;
            float ay_g = (float)mpu.ay * inv_lsb;
            float az_g = (float)mpu.az * inv_lsb;

            /* |a|^2 - 1g^2 → 偏离重力的平方幅值 (始终 >= 0) */
            float mag_sq = ax_g * ax_g + ay_g * ay_g + az_g * az_g;
            float diff = mag_sq - GRAVITY_SQ;
            if (diff < 0.0f) diff = -diff;   /* fabsf 替代 */

            /* 滑动窗口更新 */
            sum_sq -= acc_sq[sq_idx];
            acc_sq[sq_idx] = diff;
            sum_sq += diff;
            sq_idx = (sq_idx + 1) % MOTION_WINDOW;

            /* 每 10 个采样输出一次 (10Hz 运动分数更新) */
            if ((seq % 10) == 0) {
                float avg_sq = sum_sq / (float)MOTION_WINDOW;

                /* 平方幅值 → 归一化分数 0~1000 */
                mot.score = (uint16_t)(avg_sq * 400.0f);
                if (mot.score > 1000) mot.score = 1000;

                /* 活动等级分类 (阈值基于经验) */
                if      (mot.score < 40)  mot.level = 0;  /* 静止 */
                else if (mot.score < 120) mot.level = 1;  /* 微动 */
                else if (mot.score < 300) mot.level = 2;  /* 步行 */
                else if (mot.score < 550) mot.level = 3;  /* 跑步 */
                else                      mot.level = 4;  /* 剧烈 */

                xQueueSend(xMotionQueue, &mot, 0);

                frame_len = proto_pack_motion(seq, mot.score, mot.level, frame_buf);
                proto_send(frame_buf, frame_len);
            }
            seq++;
        }
    }
}

/* =================================================================== */
/* 协议发送任务 — 心率 + 心跳                                              */
/* =================================================================== */

static void vTxTask(void *pvParameters)
{
    HRResult_t hr;
    uint8_t    seq = 0;
    uint8_t    frame_buf[FRAME_BUF_SIZE];
    uint8_t    frame_len;
    TickType_t xLastWakeTime;
    (void)pvParameters;

    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        /* 心率帧 (非阻塞接收, 有新值才发) */
        if (xQueueReceive(xHRQueue, &hr, 0) == pdPASS) {
            frame_len = proto_pack_heart_rate(
                seq, hr.hr, hr.spo2_x10, hr.hr_valid, hr.spo2_valid,
                frame_buf);
            proto_send(frame_buf, frame_len);
            seq++;
        }

        /* 心跳帧 (每 1 秒) */
        frame_len = proto_pack_heartbeat(seq, frame_buf);
        proto_send(frame_buf, frame_len);
        seq++;

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(HB_INTERVAL_MS));
    }
}

/* =================================================================== */
/* 钩子函数                                                               */
/* =================================================================== */

/* 调试用: 栈溢出时记录是哪个任务 */
static volatile char *g_overflow_task = NULL;
static volatile TaskHandle_t g_overflow_handle = NULL;

void vApplicationMallocFailedHook_impl(void)
{
    taskDISABLE_INTERRUPTS();
    while (1);
}

void vApplicationStackOverflowHook_impl(TaskHandle_t xTask, char *pcTaskName)
{
    g_overflow_task   = pcTaskName;
    g_overflow_handle = xTask;
    taskDISABLE_INTERRUPTS();
    while (1);
}

/* =================================================================== */
/* 主入口                                                                 */
/* =================================================================== */

int main(void)
{
    /* 1. 系统时钟 */
    SystemClock_Config();

    /* 2. NVIC 优先级分组 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* 3. 硬件初始化 */
    Delay_init();
    uart_init(115200);
    uart_printf_mutex_init();
    OLED_Init();

    printf("\r\n=== MAX RTOS 多传感器 ===\r\n");

    /* 4. MPU6050 初始化 (I2C2 + 寄存器配置 + WHO_AM_I 验证) */
    mpu_Init();   /* 内部打印 WHO_AM_I 验证结果 */

    /* 5. 创建内核对象 */
    xI2CMutex    = xSemaphoreCreateMutex();
    xOLEDMutex   = xSemaphoreCreateMutex();
    xSensorQueue = xQueueCreate(1, sizeof(SensorData_t));
    xHRQueue     = xQueueCreate(1, sizeof(HRResult_t));
    xMpuQueue    = xQueueCreate(8, sizeof(MpuData_t));
    xMotionQueue = xQueueCreate(2, sizeof(MotionResult_t));

    /* 6. 创建任务 */
    xTaskCreate(vSensorTask, "Sens",   SENSOR_STACK, NULL, tskIDLE_PRIORITY + 4, NULL);
    xTaskCreate(vAlgoTask,   "Algo",   ALGO_STACK,   NULL, tskIDLE_PRIORITY + 3, NULL);
    xTaskCreate(vMpuTask,    "MPU",    MPU_STACK,    NULL, tskIDLE_PRIORITY + 3, NULL);
    xTaskCreate(vMotionTask, "Motion", MOTION_STACK, NULL, tskIDLE_PRIORITY + 2, NULL);
    xTaskCreate(vTxTask,     "Tx",     TX_STACK,     NULL, tskIDLE_PRIORITY + 2, NULL);

    printf("Tasks: Sens Algo MPU Motion Tx\r\n");
    printf("Starting scheduler...\r\n");
    vTaskStartScheduler();

    while (1);
}
