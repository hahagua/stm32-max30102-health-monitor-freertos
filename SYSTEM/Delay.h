#ifndef __DELAY_H
#define __DELAY_H
#include "sys.h"

void Delay_init(void);

#if SYSTEM_SUPPORT_OS  // 使用 FreeRTOS 时
    #include "FreeRTOS.h"
    #include "task.h"
    // 微秒延时：使用 DWT 硬件周期计数器 (不依赖 SysTick)
    void Delay_us(uint32_t us);
    // 毫秒延时：调用 FreeRTOS 的 vTaskDelay (任务阻塞，不占 CPU)
    void Delay_ms(uint32_t ms);
    // 秒级延时
    void Delay_s(uint32_t s);
#else  // 裸机模式
    void Delay_us(uint32_t us);
    void Delay_ms(uint32_t ms);
    void Delay_s(uint32_t s);
#endif

#endif
