/**
 * esp_simple.c — ESP8266 透传驱动
 */
#include "stm32f10x.h"
#include "Delay.h"
#include <string.h>
#include <stdio.h>

#define RX_ITER_PER_MS  6000

static void esp_tx_byte(uint8_t ch)
{
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    USART_SendData(USART2, ch);
}

static void esp_cmd(const char *s)
{
    while (*s) esp_tx_byte((uint8_t)*s++);
    esp_tx_byte('\r'); esp_tx_byte('\n');
}

static int esp_wait_for(const char *keyword, int timeout_ms)
{
    int kw_len = strlen(keyword);
    int match = 0;
    volatile uint32_t total = (uint32_t)timeout_ms * RX_ITER_PER_MS;
    while (total > 0) {
        if (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == SET) {
            char ch = (char)(USART_ReceiveData(USART2) & 0xFF);
            if (ch == keyword[match]) {
                match++;
                if (match == kw_len) return 1;
            } else {
                match = (ch == keyword[0]) ? 1 : 0;
            }
            total = (uint32_t)timeout_ms * RX_ITER_PER_MS;
        }
        __NOP(); total--;
    }
    return 0;
}

void esp_simple_init(void)
{
    GPIO_InitTypeDef g; USART_InitTypeDef u;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    g.GPIO_Pin = GPIO_Pin_4; g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz; GPIO_Init(GPIOA, &g);

    g.GPIO_Pin = GPIO_Pin_2; g.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &g);
    g.GPIO_Pin = GPIO_Pin_3; g.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &g);

    GPIO_ResetBits(GPIOA, GPIO_Pin_4);  Delay_ms(500);
    GPIO_SetBits(GPIOA, GPIO_Pin_4);    Delay_ms(2000);

    u.USART_BaudRate = 115200; u.USART_WordLength = USART_WordLength_8b;
    u.USART_StopBits = USART_StopBits_1; u.USART_Parity = USART_Parity_No;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &u);
    USART_Cmd(USART2, ENABLE);
}

int esp_simple_connect(const char *ssid, const char *pwd,
                       const char *ip, uint16_t port)
{
    static char cmd[128];
    (void)port;

    /* AT 软复位 */
    esp_cmd("AT+RST");                    Delay_ms(3000);

    esp_cmd("ATE0");                    Delay_ms(500);
    esp_cmd("AT+CWMODE=1");             Delay_ms(500);

    /* CWJAP */
    strcpy(cmd,"AT+CWJAP=\""); strcat(cmd,ssid);
    strcat(cmd,"\",\""); strcat(cmd,pwd); strcat(cmd,"\"");
    esp_cmd(cmd);                       Delay_ms(8000);

    /* 透传模式 (在 CIPSTART 之前) */
    esp_cmd("AT+CIPMODE=1");            Delay_ms(500);

    /* CIPSTART */
    strcpy(cmd,"AT+CIPSTART=\"TCP\",\"");
    strcat(cmd,ip); strcat(cmd,"\",8888");
    esp_cmd(cmd);                       Delay_ms(5000);

    /* 进入透传 */
    esp_cmd("AT+CIPSEND");              Delay_ms(3000);

    printf("[ESP] 就绪!\r\n");
    return 0;
}

void esp_simple_send(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++) esp_tx_byte(data[i]);
}
