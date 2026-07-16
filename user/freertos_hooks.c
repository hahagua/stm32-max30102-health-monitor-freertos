/**
  ******************************************************************************
  * @file    freertos_hooks.c
  * @brief   FreeRTOS 钩子函数实现 (独立文件, 不动 main.c)
  *
  * FreeRTOSConfig.h 通过宏将标准钩子重定向到 _impl 后缀函数:
  *   vApplicationMallocFailedHook  → vApplicationMallocFailedHook_impl
  *   vApplicationStackOverflowHook → vApplicationStackOverflowHook_impl
  *
  * 本文件提供这两个函数的实现, 供 FreeRTOS 内核链接.
  * 裸机编译时 (SYSTEM_SUPPORT_OS=0) 这些函数不会被调用,
  * 但链接器仍需找到它们 —— 此处提供空实现.
  ******************************************************************************
  */
#include "stm32f10x.h"
#include "sys.h"

#if SYSTEM_SUPPORT_OS
#include "FreeRTOS.h"
#include "task.h"
#endif

/*----------------------------------------------------------------------------*/
/* 内存分配失败钩子                                                           */
/*----------------------------------------------------------------------------*/
void vApplicationMallocFailedHook_impl(void)
{
#if SYSTEM_SUPPORT_OS
    taskDISABLE_INTERRUPTS();
#endif
    while (1)
    {
    }
}

/*----------------------------------------------------------------------------*/
/* 栈溢出钩子                                                                  */
/*----------------------------------------------------------------------------*/
void vApplicationStackOverflowHook_impl(TaskHandle_t xTask,
                                         char * pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
#if SYSTEM_SUPPORT_OS
    taskDISABLE_INTERRUPTS();
#endif
    while (1)
    {
    }
}
