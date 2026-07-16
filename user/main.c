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

#define BUFFER_SIZE   ((uint8_t)(FS * ST))

typedef struct {
    uint32_t ir[BUFFER_SIZE];
    uint32_t red[BUFFER_SIZE];
} SensorData_t;

static QueueHandle_t xSensorQueue = NULL;
static SemaphoreHandle_t xOLEDMutex = NULL;

#define SENSOR_STACK  384
#define ALGO_STACK    512

static void vSensorTask(void *pvParameters)
{
    uint8_t uch_dummy, temp[6];
    static SensorData_t xData;
    int32_t n;
    (void)pvParameters;

    max30102_reset();
    vTaskDelay(pdMS_TO_TICKS(1000));
    maxim_max30102_read_reg(REG_INTR_STATUS_1, &uch_dummy);
    max30102_init();

    uch_dummy = max30102_Bus_Read(REG_PART_ID);
    xSemaphoreTake(xOLEDMutex, portMAX_DELAY);
    OLED_ShowHexNum(1, 1, uch_dummy, 4);
    xSemaphoreGive(xOLEDMutex);
    printf("ID=0x%02X\r\n", uch_dummy);

    while (1) {
        for (n = 0; n < BUFFER_SIZE; n++) {
            vTaskDelay(pdMS_TO_TICKS(40));  /* FS=25 -> 40ms per sample */
            max30102_FIFO_ReadBytes(REG_FIFO_DATA, temp);
            xData.red[n] = ((uint32_t)(temp[0]&0x03)<<16) | ((uint32_t)temp[1]<<8) | temp[2];
            xData.ir[n]  = ((uint32_t)(temp[3]&0x03)<<16) | ((uint32_t)temp[4]<<8) | temp[5];
        }
        xQueueSend(xSensorQueue, &xData, portMAX_DELAY);
    }
}

static void vAlgoTask(void *pvParameters)
{
    static SensorData_t xData;
    float sp, ra, co;
    int8_t sv, hv;
    int32_t hr;
    (void)pvParameters;

    while (1) {
        if (xQueueReceive(xSensorQueue, &xData, portMAX_DELAY) == pdPASS) {
            rf_heart_rate_and_oxygen_saturation(
                xData.ir, BUFFER_SIZE, xData.red,
                &sp, &sv, &hr, &hv, &ra, &co);

            if (hv == 1 && sv == 1) {
                printf("HR=%i, SpO2=%i\r\n", (int)hr, (int)sp);
                xSemaphoreTake(xOLEDMutex, portMAX_DELAY);
                OLED_ShowString(2, 1, "HR:");  OLED_ShowNum(2, 4, hr, 3);
                OLED_ShowString(3, 1, "SpO2:"); OLED_ShowNum(3, 6, (uint16_t)sp, 3);
                OLED_ShowString(3, 9, "%");
                xSemaphoreGive(xOLEDMutex);
            }
        }
    }
}

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

void vApplicationMallocFailedHook_impl(void) { taskDISABLE_INTERRUPTS(); while(1); }
void vApplicationStackOverflowHook_impl(TaskHandle_t x, char *n) { (void)x;(void)n; taskDISABLE_INTERRUPTS(); while(1); }

int main(void)
{
    SystemClock_Config();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    OLED_Init();
    Delay_init();
    uart_init(115200);
    uart_printf_mutex_init();

    xOLEDMutex = xSemaphoreCreateMutex();
    xSensorQueue = xQueueCreate(1, sizeof(SensorData_t));

    xTaskCreate(vSensorTask, "Sens", SENSOR_STACK, NULL, tskIDLE_PRIORITY+3, NULL);
    xTaskCreate(vAlgoTask,   "Algo", ALGO_STACK,   NULL, tskIDLE_PRIORITY+2, NULL);

    vTaskStartScheduler();
    while(1);
}
