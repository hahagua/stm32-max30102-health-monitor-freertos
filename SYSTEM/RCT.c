#include "stm32f10x.h"                  // Device header


void rtc_Init(void)
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR,ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_BKP,ENABLE);
	
	PWR_BackupAccessCmd(ENABLE);
	
	RCC_LSEConfig(RCC_LSE_ON);
	while (RCC_GetFlagStatus(RCC_FLAG_LSERDY)!=SET);
	
	RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
	RCC_RTCCLKCmd(ENABLE);
	
	RTC_WaitForLastTask();
	RTC_WaitForSynchro();
	
	RTC_SetPrescaler(32768-1);
	RTC_WaitForLastTask();
	
	RTC_SetCounter(1672588795);
	RTC_WaitForLastTask();
	
}
