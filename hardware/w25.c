#include "stm32f10x.h"                  // Device header
#include "spi.h"
#include "w25_ins.h"

void w25_Init(void)
{
	spi_Init();
	
}

void w25_read(uint8_t *mid,uint16_t *did)
{
	spi_start();
	spi_swapbyte(W25Q64_JEDEC_ID);
	*mid=spi_swapbyte(W25Q64_DUMMY_BYTE);
	*did=spi_swapbyte(W25Q64_DUMMY_BYTE);
	*did<<=8;
	*did|=spi_swapbyte(W25Q64_DUMMY_BYTE);
	spi_stop();
	
}
void w25_writenable(void)
{
	spi_start();
	spi_swapbyte(W25Q64_WRITE_ENABLE);
	
}

void w25_waitbusy(void)
{
	uint16_t time;
	spi_start();
	spi_swapbyte(W25Q64_READ_STATUS_REGISTER_1);
	
	time=10000;
	while ((spi_swapbyte(W25Q64_DUMMY_BYTE)&0x01)==0x01)
	{
		time--;
		if(time==0)
		{
			break;
		}
	}
}
	
void w25_page(uint32_t address,uint8_t *dataarray,uint16_t count)
{
	w25_writenable();
	
	spi_start();
	spi_swapbyte(W25Q64_PAGE_PROGRAM);
	spi_swapbyte(address>>16);
	spi_swapbyte(address>>8);

	spi_swapbyte(address);
	uint8_t i;
	for (i=0;i<count;i++)
	{
		spi_swapbyte(dataarray[i]);
	}
	spi_stop();
	
	w25_waitbusy();
}
	
void w25_erase(uint32_t address)
{
	w25_writenable();
	
	spi_start();
	spi_swapbyte(W25Q64_SECTOR_ERASE_4KB);
	spi_swapbyte(address>>16);
	spi_swapbyte(address>>8);

	spi_swapbyte(address);
	spi_stop();
	
	w25_waitbusy();
}
void w25_readdata(uint32_t address,uint8_t *dataarray,uint32_t count)
{
	uint32_t i;
	spi_start();
	spi_swapbyte(W25Q64_READ_DATA);
	spi_swapbyte(address>>16);
	spi_swapbyte(address>>8);
	spi_swapbyte(address);
	for (i=0;i<count;i++)
	{
		dataarray[i]=spi_swapbyte(W25Q64_DUMMY_BYTE);
		
	}
	spi_stop();
}






