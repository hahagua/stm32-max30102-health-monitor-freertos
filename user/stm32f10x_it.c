/**
  ******************************************************************************
  * @file    Project/STM32F10x_StdPeriph_Template/stm32f10x_it.c
  * @author  MCD Application Team
  * @version V3.5.0
  * @date    08-April-2011
  * @brief   Main Interrupt Service Routines.
  *
  * NOTE: SVC_Handler, PendSV_Handler, SysTick_Handler are managed by
  *       FreeRTOS (port.c). DO NOT define them here when using RTOS.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f10x_it.h"
#include "sys.h"

/******************************************************************************/
/*            Cortex-M3 Processor Exceptions Handlers                         */
/******************************************************************************/

void NMI_Handler(void)
{
}

/**
  * @brief  Hard Fault handler - triggers breakpoint or infinite loop
  */
void HardFault_Handler(void)
{
    /* Go to infinite loop when Hard Fault exception occurs */
    while (1)
    {
    }
}

void MemManage_Handler(void)
{
    while (1)
    {
    }
}

void BusFault_Handler(void)
{
    while (1)
    {
    }
}

void UsageFault_Handler(void)
{
    while (1)
    {
    }
}

#if !SYSTEM_SUPPORT_OS
/**
  * @brief  SVC_Handler - managed by FreeRTOS port.c when RTOS is enabled.
  *         Only defined here for bare-metal mode.
  */
void SVC_Handler(void)
{
}
#endif

void DebugMon_Handler(void)
{
}

#if !SYSTEM_SUPPORT_OS
/**
  * @brief  PendSV_Handler - managed by FreeRTOS port.c when RTOS is enabled.
  *         FreeRTOS uses PendSV for context switching.
  */
void PendSV_Handler(void)
{
}
#endif

#if !SYSTEM_SUPPORT_OS
/**
  * @brief  SysTick_Handler - managed by FreeRTOS port.c when RTOS is enabled.
  *         FreeRTOS uses SysTick for the system tick.
  */
void SysTick_Handler(void)
{
}
#endif

/******************************************************************************/
/*                 STM32F10x Peripherals Interrupt Handlers                   */
/******************************************************************************/

/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
