/*
 * FreeRTOS Kernel V10.6.2
 * Configuration for STM32F103C8 (Cortex-M3, 36MHz, 20KB SRAM)
 *
 * This is a project-specific configuration file for FreeRTOS.
 * Tailored for: MAX30102 heart rate / SpO2 monitor
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*-----------------------------------------------------------
 * Application specific definitions.
 *
 * NOTE: The FreeRTOS interrupt handlers (SVC_Handler, PendSV_Handler,
 *       SysTick_Handler) were renamed directly in port.c to match the
 *       STM32 startup vector table names. This avoids ARMCC __asm
 *       preprocessor limitations with #define renaming.
 *----------------------------------------------------------*/
#define configUSE_PREEMPTION                    1           /* 抢占式调度器 */
#define configUSE_IDLE_HOOK                     0           /* 不使用空闲钩子 */
#define configUSE_TICK_HOOK                     0           /* 不使用滴答钩子 */
#define configCPU_CLOCK_HZ                      ( ( unsigned long ) 36000000 )  /* 36MHz (HSI/2*9) */
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )         /* 1ms 系统节拍 */
#define configMAX_PRIORITIES                    ( 16 )      /* 最大16个优先级 */
#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) 128 )      /* 最小任务栈 128 words = 512 bytes */
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 12 * 1024 ) )    /* 堆大小 12KB */
#define configMAX_TASK_NAME_LEN                 ( 16 )      /* 任务名最大16字符 */
#define configUSE_TRACE_FACILITY                1           /* 启用跟踪功能(用于vTaskList) */
/* configUSE_16_BIT_TICKS removed — use configTICK_TYPE_WIDTH_IN_BITS instead (V10.6.x) */
#define configIDLE_SHOULD_YIELD                 1           /* 空闲任务可让出CPU */
#define configUSE_MUTEXES                       1           /* 启用互斥信号量 */
#define configQUEUE_REGISTRY_SIZE               8           /* 队列注册表大小 */
#define configCHECK_FOR_STACK_OVERFLOW          2           /* 栈溢出检测(方法2) */
#define configUSE_RECURSIVE_MUTEXES             1           /* 递归互斥量 */
#define configUSE_COUNTING_SEMAPHORES           1           /* 计数信号量 */
#define configUSE_TIMERS                        1           /* 启用软件定时器 */
/* Timer config not needed when configUSE_TIMERS=0 */
#define configTIMER_TASK_PRIORITY               ( 3 )
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            ( 128 )

/* Co-routine definitions (not used) */
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         ( 2 )

/* Software timer priority and stack, using CMSIS-RTOS convention */
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

/* Set the following definitions to 1 to include the API function, or zero
 * to exclude the API function. */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskCleanUpResources           0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xEventGroupSetBitsFromISR       1
#define INCLUDE_xQueueGetMutexHolder            1
#define INCLUDE_eTaskGetState                   1

/*-----------------------------------------------------------
 * Cortex-M3 specific interrupt priority configuration.
 *
 * STM32 NVIC uses bits [7:4] of the priority register.
 * NVIC_PriorityGroup_2: 2-bit preemption, 2-bit sub-priority.
 *
 * configKERNEL_INTERRUPT_PRIORITY:
 *   Set PendSV and SysTick to lowest priority (255 = priority 15).
 *
 * configMAX_SYSCALL_INTERRUPT_PRIORITY:
 *   BASEPRI threshold = 191 (priority 11).
 *   Interrupts at priority 0-10 can call FreeRTOS API "FromISR" functions.
 *   Interrupts at priority 11-15 are fully masked during critical sections.
 *----------------------------------------------------------*/
#define configKERNEL_INTERRUPT_PRIORITY         255
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    176  /* 0xB0 = priority 11, low nibble must be 0 for STM32F1 */
/* Equivalent value for STM32 standard library NVIC priority (0-15 scale) */
#define configLIBRARY_KERNEL_INTERRUPT_PRIORITY 15

/*-----------------------------------------------------------
 * Tick type width. 32-bit tick counter on a 32-bit architecture.
 *----------------------------------------------------------*/
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS

/*-----------------------------------------------------------
 * Assert function for debugging.
 *----------------------------------------------------------*/
#define configASSERT( x )    if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); }

/*-----------------------------------------------------------
 * Define a function to retrieve the runtime counter for vTaskGetRunTimeStats.
 *----------------------------------------------------------*/
#define configGENERATE_RUN_TIME_STATS           0
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
#define portGET_RUN_TIME_COUNTER_VALUE()        0

/*-----------------------------------------------------------
 * Optional functions - most optimal for Cortex-M3 with ARMCC.
 *----------------------------------------------------------*/
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1

/*-----------------------------------------------------------
 * Hooks to call when memory allocation fails and on stack overflow.
 *----------------------------------------------------------*/
#define vApplicationMallocFailedHook()          vApplicationMallocFailedHook_impl()
#define vApplicationStackOverflowHook( xTask, pcTaskName )  vApplicationStackOverflowHook_impl( xTask, pcTaskName )

#endif /* FREERTOS_CONFIG_H */
