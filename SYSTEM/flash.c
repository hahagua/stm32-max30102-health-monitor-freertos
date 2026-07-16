#include "stm32f10x.h"                  // Device header

uint32_t flash_readword(uint32_t address)
{
	return *((__IO uint32_t *)(address));

}
uint32_t flash_readhalfword(uint32_t address)
{
	return *((__IO uint32_t *)(address));

}
uint32_t flash_readbyte(uint32_t address)
{
	return *((__IO uint32_t *)(address));

}

void flash_eraseallpage(void)
{
	FLASH_Unlock();
	FLASH_EraseAllPages();
	FLASH_Lock();

}
void flash_erasepage(uint32_t pageaddress)
{
	FLASH_Unlock();
	FLASH_ErasePage(pageaddress);
	FLASH_Lock();

}

void flash_program(uint32_t address ,uint32_t data)
{
	FLASH_Unlock();
	FLASH_ProgramWord(address,data);
	FLASH_Lock();

}




