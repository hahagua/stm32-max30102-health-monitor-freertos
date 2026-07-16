#include "stm32f10x.h"                  // Device header
#include "Delay.h"

//#define SCL_PORT    GPIO_B
//#define SCL_PIN     GPIOB_Pin_10    宏定义

void i2c_w_SCL(uint8_t BitValue)
{
	GPIO_WriteBit(GPIOB,GPIO_Pin_10,(BitAction)BitValue);
	Delay_us(10);

}

void i2c_w_SDA(uint8_t BitValue)
{
	GPIO_WriteBit(GPIOB,GPIO_Pin_11,(BitAction)BitValue);
	Delay_us(10);

}

uint8_t i2C_r_SDA(void)
{
	uint8_t BitValue;
	BitValue=GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_11);
	Delay_us(10);
	return BitValue;
}

void i2c_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
	
	
	GPIO_InitTypeDef GPIO_Initstructure;
	GPIO_Initstructure.GPIO_Mode=GPIO_Mode_Out_OD;
	GPIO_Initstructure.GPIO_Pin=GPIO_Pin_10|GPIO_Pin_11;
	GPIO_Initstructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOB,&GPIO_Initstructure);
	
	GPIO_SetBits(GPIOB,GPIO_Pin_10 | GPIO_Pin_11);

}

void i2c_start(void)
{
	i2c_w_SDA(1);
	i2c_w_SCL(1);
	i2c_w_SDA(0);
	i2c_w_SCL(0);
	
	
}

void i2c_stop(void)
{
	i2c_w_SDA(0);
	i2c_w_SCL(1);
	i2c_w_SDA(1);
	
}

void i2c_sendbyte(uint8_t byte)
{
	uint8_t i;
	for (i=0;i<8;i++)
	
	{	i2c_w_SDA(byte & (0x80>>i));
		i2c_w_SCL(1);
		i2c_w_SCL(0);
	}
	
	
}	
	
uint8_t i2c_receive(void)
{
	
	uint8_t i, byte=0x00;
	i2c_w_SDA(1);
	for (i=0;i<8;i++)
	{	i2c_w_SCL(1);
		if (i2C_r_SDA()==1){byte|=(0x80>>i);}
		i2c_w_SCL(0);
	}
	return byte;
	
}
void i2c_sendack(uint8_t ack)
{
	

	
	{	i2c_w_SDA(ack);
		i2c_w_SCL(1);
		i2c_w_SCL(0);
	}
	
	
}	
	
uint8_t i2c_receiveack(void)
{
	
	uint8_t ack;
	i2c_w_SDA(1);
	
	i2c_w_SCL(1);
	ack=i2C_r_SDA();
	i2c_w_SCL(0);
	
	return ack;
	
}
