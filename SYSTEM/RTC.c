#include "stm32f10x.h"                  // Device header
#include <time.h>

uint16_t RTC_time[]={2023,1,1,23,59,55};

void rtc_Init(void)
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR,ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_BKP,ENABLE);
	
	PWR_BackupAccessCmd(ENABLE);
	
	if(BKP_ReadBackupRegister(BKP_DR1)!=0x1010)
	{
	
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
		
		BKP_WriteBackupRegister(BKP_DR1,0x1010);
	}
	else
	{
		RTC_WaitForLastTask();
		RTC_WaitForSynchro();	
	}
}
void rtc_settime(void)
{
	time_t time_cnt;
	struct  tm time_data;
	time_data.tm_year=RTC_time[0]-1900;
	time_data.tm_mon=RTC_time[1]-1;
	time_data.tm_mday=RTC_time[2];
	time_data.tm_hour=RTC_time[3];
	time_data.tm_min=RTC_time[4];
	time_data.tm_sec=RTC_time[5];

	time_cnt=mktime(&time_data);
	
	RTC_SetCounter(time_cnt);
	
}
void rtc_readtime(void)
{
	time_t time_cnt;
	struct  tm time_data;
	time_cnt=RTC_GetCounter();
	
	time_data=*localtime(&time_cnt);
	
	RTC_time[0]=time_data.tm_year+1900;
	RTC_time[1]=time_data.tm_mon+1;
    RTC_time[2]=time_data.tm_mday;
    RTC_time[3]=time_data.tm_hour;
    RTC_time[4]=time_data.tm_min;
    RTC_time[5]=time_data.tm_sec;


} 

