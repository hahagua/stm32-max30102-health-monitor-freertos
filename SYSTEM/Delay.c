#include "delay.h"
#include "stm32f10x.h"

#if SYSTEM_SUPPORT_OS  // ================ FreeRTOS 模式 ================

/*
 * STM32F10x StdPeriph 的 core_cm3.h 未定义 DWT 外设寄存器.
 * DWT (Data Watchpoint and Trace) 是 Cortex-M3 调试单元,
 * 提供硬件周期计数器, 与 SysTick 独立, 不干扰 FreeRTOS.
 * 此处直接通过地址访问寄存器.
 */
#define DWT_BASE    (0xE0001000UL)
#define DWT_CTRL    (*(volatile uint32_t *)(DWT_BASE + 0x00))
#define DWT_CYCCNT  (*(volatile uint32_t *)(DWT_BASE + 0x04))
#define DEMCR       (*(volatile uint32_t *)0xE000EDFCUL)
#define DEMCR_TRCENA (1UL << 24)    /* CoreDebug_DEMCR_TRCENA_Msk */
#define DWT_CTRL_CYCCNTENA (1UL << 0)

void Delay_init(void)
{
    DEMCR |= DEMCR_TRCENA;    /* 使能 DWT 跟踪 */
    DWT_CYCCNT = 0;            /* 清零计数器 */
    DWT_CTRL  |= DWT_CTRL_CYCCNTENA;  /* 使能周期计数器 */
}

void Delay_us(uint32_t xus)
{
    uint32_t ticks_start = DWT_CYCCNT;
    uint32_t ticks_needed = (SystemCoreClock / 1000000) * xus;

    while ((DWT_CYCCNT - ticks_start) < ticks_needed)
    {
        /* 忙等待, 用于微秒级精确延时 */
    }
}

/**
  * @brief  毫秒级延时 (调用 FreeRTOS 阻塞延迟)
  * @param  xms 延时时长 (ms)
  * @note   任务进入阻塞态, 释放 CPU 给其他任务
  *         最小阻塞单位 = 1个 tick = 1ms (configTICK_RATE_HZ=1000)
  *         如果 xms=0, 不阻塞但让出CPU
  * @retval 无
  */
void Delay_ms(uint32_t xms)
{
    if (xms == 0)
    {
        /* 0ms 延时: 仅触发一次任务调度 */
        taskYIELD();
        return;
    }

    /* FreeRTOS 阻塞延迟: 任务挂起 xms 个 tick */
    /* 注: vTaskDelay 的精度为 1 tick (1ms),
       xms 个 tick = xms ms (tick rate = 1000Hz) */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskDelay(pdMS_TO_TICKS(xms));
    }
    else
    {
        /* 调度器未启动时的降级方案: 使用 DWT 忙等待 */
        /* 这种情况只在 main() 初始化阶段出现 */
        uint32_t i;
        for (i = 0; i < xms; i++)
        {
            Delay_us(1000);
        }
    }
}

/**
  * @brief  秒级延时
  * @param  xs 延时时长 (s)
  * @retval 无
  */
void Delay_s(uint32_t xs)
{
    while (xs--)
    {
        Delay_ms(1000);
    }
}

#else  // ================ 裸机模式 ================

/**
  * @brief  延时初始化 (裸机模式: 关闭SysTick, 由Delay_us按需使用)
  * @param  无
  * @retval 无
  */
void Delay_init(void)
{
    SysTick->CTRL = 0;  // 关闭SysTick
}

/**
  * @brief  微秒级延时 (裸机: 使用 SysTick)
  * @param  xus 延时时长，范围：0~233015
  * @note   SysTick 时钟 = HCLK = 72MHz
  * @retval 无
  */
void Delay_us(uint32_t xus)
{
    SysTick->LOAD = 72 * xus;              // 设置定时器重装值 (72MHz)
    SysTick->VAL  = 0x00;                  // 清空当前计数值
    SysTick->CTRL = 0x00000005;            // 设置时钟源为HCLK，启动定时器
    while(!(SysTick->CTRL & 0x00010000));  // 等待计数到0
    SysTick->CTRL = 0x00000004;            // 关闭定时器
}

/**
  * @brief  毫秒级延时 (裸机)
  * @param  xms 延时时长
  * @retval 无
  */
void Delay_ms(uint32_t xms)
{
    while(xms--)
    {
        Delay_us(1000);
    }
}

/**
  * @brief  秒级延时 (裸机)
  * @param  xs 延时时长
  * @retval 无
  */
void Delay_s(uint32_t xs)
{
    while(xs--)
    {
        Delay_ms(1000);
    }
}

#endif  // SYSTEM_SUPPORT_OS
