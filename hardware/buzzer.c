#include "stm32f10x.h"                  // Device header


void Buzzer_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);//使能GPIOA的时钟
	
	
	GPIO_InitTypeDef GPIO_Initstructure;//定义结构体
	GPIO_Initstructure.GPIO_Mode=GPIO_Mode_Out_PP;//设置输出模式pp
	GPIO_Initstructure.GPIO_Pin=GPIO_Pin_12|GPIO_Pin_13;//选择引脚1,2
	GPIO_Initstructure.GPIO_Speed=GPIO_Speed_50MHz;//设置50hz
	GPIO_Init(GPIOB,&GPIO_Initstructure);//初始化引脚，取第一行定义的结构体地址，把下面三个结构体内容设置给GPIOA
	
	GPIO_SetBits(GPIOA,GPIO_Pin_12 | GPIO_Pin_13);//设置为高电平
	
}
void Buzzer_ON(void)//点亮
{
	GPIO_ResetBits(GPIOB,GPIO_Pin_12);//设置为低电平（如果上面设置的是低电平这里就点亮）
	
}
void Buzzer_OFF(void)//关闭
{
	GPIO_SetBits(GPIOB,GPIO_Pin_12);//设置为高电平
	
}
void Buzzer_Turn(void)//转换
{
	if(GPIO_ReadOutputDataBit(GPIOB,GPIO_Pin_12)==0)//判断输出电平是否为低电平（这个函数默认判断低电平）
	{
		GPIO_SetBits(GPIOB,GPIO_Pin_12);//如果是真，设置为高电平灯灭
		
	}	
	else
	{
		GPIO_ResetBits(GPIOB,GPIO_Pin_12);//如果是假设置为低电平灯亮
	}
}



