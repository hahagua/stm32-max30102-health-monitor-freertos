#include "stm32f10x.h"   // Device header
#include "Delay.h"


void Key_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
	
	GPIO_InitTypeDef GPIO_Initstructure;
	GPIO_Initstructure.GPIO_Mode=GPIO_Mode_IPU;
	GPIO_Initstructure.GPIO_Pin=GPIO_Pin_1 | GPIO_Pin_11;
	GPIO_Initstructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOB,&GPIO_Initstructure);
	
	
}

uint8_t Key_GetNum(void)
{
	uint8_t KeyNum=0;
	if (GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_1)==0)        //默认判断是否为低电平（依靠上拉电阻）未按下为高电平（=1），按下后为低电平=0
	{
		Delay_ms(20);                          //- 按键断开，电流从VCC经上拉电阻流向PB1引脚， PB1检测到高电平（3.3V），所以输入状态是 1
		while (GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_1)==0);
		Delay_ms(20);
		KeyNum =1;                                        //按下后，PB1引脚直接连接到GND（0V）为低电平
		if (GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_11)==0)
	
		Delay_ms(20);
		while (GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_11)==0);
		Delay_ms(20);
		KeyNum =2;
		
		
	}
	return KeyNum;
}
