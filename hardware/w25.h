#ifndef __W25_H
#define __W25_H

void w25_Init(void);

void w25_read(uint8_t *mid,uint16_t *did);

void w25_page(uint32_t address,uint8_t *dataarray,uint16_t count);

void w25_erase(uint32_t address);

void w25_readdata(uint32_t address,uint8_t *dataarray,uint32_t count);

#endif
