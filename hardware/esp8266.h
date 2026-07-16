#ifndef __ESP8266_H
#define __ESP8266_H

#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* ============================================================
 * ESP8266 WiFi 模块 AT 驱动 — USART2
 *
 * 硬件连接 (默认引脚, STM32F103C8 48-pin):
 *   PA2 — USART2_TX → ESP8266 RX
 *   PA3 — USART2_RX → ESP8266 TX
 *
 * ⚠️ 引脚冲突说明:
 *   PA2 在现有工程中被 LED2 占用 (hardware/LED.c)。
 *   建议将 LED2 移至空闲引脚 PA15 或 PB15，
 *   或在 64+ pin 封装上开启 USART2 Remap → PD5/PD6。
 *
 * ESP8266 供电: 3.3V (峰值电流 ~300mA, 需独立 LDO)
 * 波特率: 115200 (模块出厂默认)
 * 协议: AT 命令, 非透传, 单连接 (TCP)
 * ============================================================ */

/* ---------- 错误码 ---------- */
typedef enum {
    ESP_OK          =  0,   /* 操作成功 */
    ESP_ERR_BUSY    = -1,   /* 模块正忙(上一命令未完成) */
    ESP_ERR_TIMEOUT = -2,   /* AT 应答超时 */
    ESP_ERR_SEND    = -3,   /* 发送失败 / 连接未建立 */
    ESP_ERR_RX      = -4,   /* 接收异常 */
    ESP_ERR_RESP    = -5,   /* 收到非预期应答 */
    ESP_ERR_WIFI    = -6,   /* WiFi 连接失败 */
    ESP_ERR_TCP     = -7,   /* TCP 连接失败 */
    ESP_ERR_STATE   = -8,   /* 状态机不允许此操作 */
    ESP_ERR_NOMEM   = -9,   /* 内存不足 */
    ESP_ERR_HW      = -10,  /* 硬件/模块未响应 */
} esp_err_t;

/* ---------- WiFi 加密模式 ---------- */
typedef enum {
    ESP_WIFI_OPEN        = 0,
    ESP_WIFI_WPA_PSK     = 2,
    ESP_WIFI_WPA2_PSK    = 3,
    ESP_WIFI_WPA_WPA2_PSK= 4,
} esp_wifi_enc_t;

/* ---------- 连接状态 ---------- */
typedef enum {
    ESP_STA_IDLE        = 0,   /* 未初始化 */
    ESP_STA_READY       = 1,   /* AT 就绪，未连 WiFi */
    ESP_STA_CONNECTING  = 2,   /* 正在连接 WiFi */
    ESP_STA_WIFI_OK     = 3,   /* WiFi 已连接，无 TCP */
    ESP_STA_TCP_CONNECT = 4,   /* TCP 连接中 */
    ESP_STA_TCP_OK      = 5,   /* TCP 已连接，可透传 */
    ESP_STA_ERROR       = 6,   /* 错误状态 */
} esp_sta_t;

/* ---------- 接收回调 (来自 +IPD 的 TCP 数据) ---------- */
typedef void (*esp_rx_callback_t)(const uint8_t *data, uint16_t len);

/* ============================================================
 * 公开 API
 * ============================================================ */

/* --- 初始化 & 复位 --- */
esp_err_t esp8266_Init(uint32_t baud);
esp_err_t esp8266_Reset(void);
esp_err_t esp8266_Restore(void);

/* --- AT 测试 --- */
esp_err_t esp8266_ATTest(void);
esp_err_t esp8266_GetVersion(char *ver, uint8_t maxlen);

/* --- WiFi 操作 --- */
esp_err_t esp8266_SetMode(uint8_t mode);           /* 1=STA, 2=AP, 3=STA+AP */
esp_err_t esp8266_ConnectAP(const char *ssid,
                            const char *pwd,
                            uint32_t timeout_ms);
esp_err_t esp8266_DisconnectAP(void);
esp_err_t esp8266_GetLocalIP(char *ip, uint8_t maxlen);

/* --- TCP 操作 (单连接模式) --- */
esp_err_t esp8266_TCPConnect(const char *remote_ip,
                             uint16_t remote_port,
                             uint32_t timeout_ms);
esp_err_t esp8266_TCPClose(void);
esp_err_t esp8266_TCPSend(const uint8_t *data, uint16_t len);
esp_err_t esp8266_TCPSendString(const char *str);

/* --- 接收回调注册 (FreeRTOS 任务中调用) --- */
void     esp8266_SetRxCallback(esp_rx_callback_t cb);

/* --- 状态查询 --- */
esp_sta_t esp8266_GetState(void);
uint8_t   esp8266_IsConnected(void);    /* 返回 1 当 TCP 链路就绪 */

/* --- 底层原始发送 (供调试/扩展 AT 命令用) --- */
esp_err_t esp8266_SendCmd(const char *cmd, const char *expect,
                           char *resp, uint16_t resp_len,
                           uint32_t timeout_ms);

#endif /* __ESP8266_H */
