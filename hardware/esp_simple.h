#ifndef __ESP_SIMPLE_H
#define __ESP_SIMPLE_H

#include "stm32f10x.h"

void esp_simple_init(void);
int  esp_simple_connect(const char *ssid, const char *pwd,
                        const char *ip, uint16_t port);
void esp_simple_send(const uint8_t *data, uint16_t len);

#endif
