#include "stm32f10x.h"                  // Device header
#include "flash.h"

uint16_t store_data[512];

void store_Init(void)
{
	if (flash_readhalfword(0x0800FC00)!=0xA5A5)
	{
		FLASH_ErasePage(0x0800FC00);
		FLASH_ProgramHalfWord(0x0800FC00,0xA5A5);
		for (uint16_t i=1;i<512;i++)
		{
			FLASH_ProgramHalfWord(0x0800FC00+i*2,0x0000);
		}
	
	}
	for (uint16_t i=0;i<512;i++)
	{
		store_data[i]=flash_readhalfword(0x0800FC00+i*2);
	}
}
void store_save(void)
{
	FLASH_ErasePage(0x0800FC00);
 	for (uint16_t i=1;i<512;i++)
	{
		FLASH_ProgramHalfWord(0x0800FC00+i*2,store_data[i]);
	}

}


