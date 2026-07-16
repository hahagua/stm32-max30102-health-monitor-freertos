#include "stm32f10x.h"                  // Device header

uint8_t serial_Txpacket[4];
uint8_t serial_Rxpacket[4];
uint8_t serial_Rxflag;    saas da sdsa   



void serial_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1,ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	
	GPIO_InitTypeDef GPIO_InitSructure;
	GPIO_InitSructure.GPIO_Mode=GPIO_Mode_AF_PP;
	GPIO_InitSructure.GPIO_Pin=GPIO_Pin_9;
	GPIO_InitSructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitSructure);
	
	GPIO_InitSructure.GPIO_Mode=GPIO_Mode_IPU;
	GPIO_InitSructure.GPIO_Pin=GPIO_Pin_10;
	GPIO_InitSructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitSructure);
	
	USART_InitTypeDef USART_InitStructure;
	USART_InitStructure.USART_BaudRate=9600;
	USART_InitStructure.USART_HardwareFlowControl=USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode=USART_Mode_Tx | USART_Mode_Rx;
	USART_InitStructure.USART_Parity=USART_Parity_No;
	USART_InitStructure.USART_StopBits=USART_StopBits_1;
	USART_InitStructure.USART_WordLength=USART_WordLength_8b;
	USART_Init(USART1,&USART_InitStructure);
	
	USART_Cmd(USART1,ENABLE);

}

void  serial_SENDDATA(uint8_t Byte)
{
	USART_SendData(USART1,Byte);
	USART_GetFlagStatus(USART1,USART_FLAG_TXE);
	while (USART_GetFlagStatus(USART1,USART_FLAG_TXE)==RESET);
	


}

void serial_SENDARRAY(uint8_t *ARRAY,uint16_t length)
{
	uint16_t i;
	for(i=0;i < length;i++)
	{
		serial_SENDDATA(ARRAY[i]);
		
	}


}

