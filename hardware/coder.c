#include "stm32f10x.h"                  // Device header

int16_t coder_coder;

void coder_Init(void)
{
RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);//GPIO开启时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO,ENABLE);//AFIO开启时钟
	GPIO_InitTypeDef GPIO_Initstructure;
	GPIO_Initstructure.GPIO_Mode=GPIO_Mode_IPU;
	GPIO_Initstructure.GPIO_Pin=GPIO_Pin_0 | GPIO_Pin_1;
	GPIO_Initstructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOB,&GPIO_Initstructure);
	
	GPIO_EXTILineConfig(GPIO_PortSourceGPIOB,GPIO_PinSource0);//AFIO外部引脚选择配置，表示连接pb0号口的第0个中断线路(固定0号接0个)
	GPIO_EXTILineConfig(GPIO_PortSourceGPIOB,GPIO_PinSource1);
	
	EXTI_InitTypeDef EXTI_InitStructure;//配置EXTI
	EXTI_InitStructure.EXTI_Line=EXTI_Line0 | EXTI_Line1;
	EXTI_InitStructure.EXTI_LineCmd=ENABLE;
	EXTI_InitStructure.EXTI_Mode=EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger=EXTI_Trigger_Falling;//下降沿触发；遮挡抬起后触发（如果是上升沿触发则遮挡就触发）
	EXTI_Init(&EXTI_InitStructure);
	
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
														//配置中断
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel=EXTI0_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd=ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority=1;
	NVIC_Init(&NVIC_InitStructure);
	
	NVIC_InitStructure.NVIC_IRQChannel=EXTI1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd=ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority=2;
	NVIC_Init(&NVIC_InitStructure);
}



int16_t coder_Get(void)
{
	int16_t Temp;		//获得变化量而不是累计量；比如第一次旋转加三，第二次选择加二，会把第一次的加三清零第二次加二而不是加5
	Temp=coder_coder;
	coder_coder=0;
	return Temp;
}


void EXTI0_IRQHandler (void)
{
	if (EXTI_GetITStatus(EXTI_Line0)==SET)
	{
		if (GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_1)==0)
		{
			coder_coder --;
		}
		EXTI_ClearITPendingBit(EXTI_Line0);
	}
	
}

void EXTI1_IRQHandler (void)
{
	if (EXTI_GetITStatus(EXTI_Line1)==SET)
	{
		
		if (GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_0)==0)
		{
			coder_coder ++;
		}
		EXTI_ClearITPendingBit(EXTI_Line1);
	}
	
}
