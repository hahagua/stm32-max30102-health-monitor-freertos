#include "esp8266.h"
#include "sys.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * ESP8266 WiFi 模块 AT 驱动 — 实现
 *
 * 硬件连接:
 *   USART2_TX  — PA2  → ESP8266 RX
 *   USART2_RX  — PA3  → ESP8266 TX
 *   波特率: 115200 (模块出厂默认)
 *
 * 协议说明:
 *   - 非透传模式 (AT+CIPMODE=0)
 *   - 单连接 (AT+CIPMUX=0)
 *   - 发送:  AT+CIPSEND=<len> → 收到 ">" → 发数据 → 等 SEND OK
 *   - 接收:  模块主动上报 +IPD,<len>:<data>
 *
 * 依赖: FreeRTOS 信号量 (AT 应答同步)
 * ============================================================ */

/* ---------- 硬件引脚 ---------- */
#define ESP_USART               USART2
#define ESP_USART_IRQn          USART2_IRQn
#define ESP_GPIO                GPIOA
#define ESP_TX_PIN              GPIO_Pin_2
#define ESP_RX_PIN              GPIO_Pin_3
#define ESP_RCC_USART           RCC_APB1Periph_USART2
#define ESP_RCC_GPIO            RCC_APB2Periph_GPIOA

/* ---------- 协议常量 ---------- */
#define ESP_CMD_TIMEOUT_MS      5000UL   /* AT 命令默认超时 */
#define ESP_CONNECT_TIMEOUT_MS  15000UL  /* WiFi/ TCP 连接超时 */
#define ESP_RX_BUF_SIZE         1024     /* 环形缓冲区大小 */
#define ESP_LINE_BUF_SIZE       512      /* 单行最大长度 */
#define ESP_RESP_BUF_SIZE       1024     /* 应答累积大小 */

/* ---------- 内部状态 ---------- */
static esp_sta_t          g_state   = ESP_STA_IDLE;
static esp_rx_callback_t  g_rx_cb   = NULL;

/* --- 环形接收缓冲区 (ISR 写 head, 任务读 tail) --- */
static uint8_t           g_rx_ring[ESP_RX_BUF_SIZE];
static volatile uint16_t g_rx_head = 0;
static volatile uint16_t g_rx_tail = 0;

/* --- AT 应答同步 --- */
static SemaphoreHandle_t  g_resp_sem  = NULL;
static char               g_resp_buf[ESP_RESP_BUF_SIZE];
static volatile uint16_t  g_resp_len  = 0;
static volatile uint8_t   g_resp_done = 0;

/* ============================================================
 * 底层 — USART2 驱动
 * ============================================================ */

static void esp_USART_Init(uint32_t baud)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    /* 时钟 */
    RCC_APB2PeriphClockCmd(ESP_RCC_GPIO,  ENABLE);
    RCC_APB1PeriphClockCmd(ESP_RCC_USART, ENABLE);

    /* PA2 — TX 复用推挽 */
    GPIO_InitStructure.GPIO_Pin   = ESP_TX_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ESP_GPIO, &GPIO_InitStructure);

    /* PA3 — RX 浮空输入 */
    GPIO_InitStructure.GPIO_Pin   = ESP_RX_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(ESP_GPIO, &GPIO_InitStructure);

    /* USART: 8N1 */
    USART_InitStructure.USART_BaudRate            = baud;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(ESP_USART, &USART_InitStructure);

    /* 使能 RXNE 中断 */
    USART_ITConfig(ESP_USART, USART_IT_RXNE, ENABLE);

    /*
     * NVIC 优先级: 抢占=3, 子=0 (最低, 不抢 SysTick)
     */
    NVIC_InitStructure.NVIC_IRQChannel                   = ESP_USART_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(ESP_USART, ENABLE);
}

/* --- 发送单字节 (阻塞) --- */
static void esp_USART_SendByte(uint8_t ch)
{
    USART_SendData(ESP_USART, ch);
    while (USART_GetFlagStatus(ESP_USART, USART_FLAG_TXE) == RESET);
}

/* --- 发送字符串 --- */
static void esp_USART_SendStr(const char *str)
{
    while (*str) {
        esp_USART_SendByte((uint8_t)*str++);
    }
}

/* --- 发送数据块 --- */
static void esp_USART_SendBuf(const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++) {
        esp_USART_SendByte(buf[i]);
    }
}

/* ============================================================
 * 环形缓冲区操作
 *
 * 策略: 单一生产者 (ISR) / 单一消费者 (任务)
 *       ARM Cortex-M3 上 uint16_t 读/写是原子的 (对齐)
 *       无需关中断保护，只要 ISR 只写 head，任务只写 tail
 * ============================================================ */

/* ISR 中调用 —— 写一字节到 ring */
static void ring_put_from_isr(uint8_t ch)
{
    uint16_t next = (g_rx_head + 1) % ESP_RX_BUF_SIZE;
    if (next != g_rx_tail) {         /* 非满 */
        g_rx_ring[g_rx_head] = ch;
        g_rx_head = next;
    }
    /* 满 → 丢弃 (增大 ESP_RX_BUF_SIZE 可避免) */
}

/* 任务中调用 —— 读一字节, 返回 1=成功 */
static uint8_t ring_get(uint8_t *ch)
{
    if (g_rx_tail == g_rx_head) return 0;   /* 空 */
    *ch = g_rx_ring[g_rx_tail];
    g_rx_tail = (g_rx_tail + 1) % ESP_RX_BUF_SIZE;
    return 1;
}

/* 环中可用字节数 */
static uint16_t ring_available(void)
{
    return (g_rx_head - g_rx_tail + ESP_RX_BUF_SIZE) % ESP_RX_BUF_SIZE;
}

/* 清空环形缓冲 (仅任务上下文调用) */
static void ring_flush(void)
{
    USART_ITConfig(ESP_USART, USART_IT_RXNE, DISABLE);
    g_rx_head = 0;
    g_rx_tail = 0;
    USART_ITConfig(ESP_USART, USART_IT_RXNE, ENABLE);
}

/* ============================================================
 * USART2 中断服务
 * ============================================================ */

void USART2_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* 接收中断 */
    if (USART_GetITStatus(ESP_USART, USART_IT_RXNE) != RESET) {
        uint8_t ch = (uint8_t)USART_ReceiveData(ESP_USART);
        ring_put_from_isr(ch);
    }

    /* 溢出错误 — 读 SR + DR 清标志 (不清会导致中断死锁) */
    if (USART_GetITStatus(ESP_USART, USART_IT_ORE) != RESET) {
        (void)USART_ReceiveData(ESP_USART);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* ============================================================
 * 行读取器
 * ============================================================ */

/*
 * 从环形缓冲区读取一行 (以 \n 结尾)
 * 返回: 1=读到完整行, 0=暂无换行符
 * 自动去掉末尾的 \r\n
 */
static uint8_t read_line(char *buf, uint16_t maxlen)
{
    uint16_t idx = 0;
    uint8_t  ch;

    while (idx < maxlen - 1) {
        if (!ring_get(&ch)) return 0;

        if (ch == '\n') {
            /* 去掉末尾 \r */
            if (idx > 0 && buf[idx - 1] == '\r') {
                idx--;
            }
            buf[idx] = '\0';
            return 1;
        }
        buf[idx++] = ch;
    }
    buf[idx] = '\0';
    return 1;   /* 超长截断 */
}

/* ============================================================
 * AT 应答解析器 (任务上下文, 每次轮询调用)
 *
 * 职责:
 *   1. 读取接收到的每一行
 *   2. 识别 +IPD 数据并调用用户回调
 *   3. 识别 WIFI CONNECTED / DISCONNECT / GOT IP 等异步事件
 *   4. 累积应答行到 g_resp_buf
 *   5. 读到 "OK" / "ERROR" / "FAIL" 时标记 g_resp_done
 *
 * 注意: 本函数在任务中调用，不直接操作信号量
 *       信号量的 give 由调用者 (esp8266_SendCmd 等待循环) 负责
 * ============================================================ */

static void parse_response(void)
{
    static char line[ESP_LINE_BUF_SIZE];  /* static 省去 512B 栈开销 */
    uint16_t free_space;

    while (read_line(line, sizeof(line))) {

        /* --- 空行跳过 --- */
        if (line[0] == '\0') continue;

        /* --- 回显跳过 (ATE0 已关闭, 但首条 AT 可能仍有) --- */
        if (strncmp(line, "AT", 2) == 0) continue;

        /* ================================================
         * +IPD,<len>:<data> — TCP 接收数据
         * 数据在冒号后, 同在一行内 (文本协议)
         * ================================================ */
        if (strncmp(line, "+IPD,", 5) == 0) {
            char *colon = strchr(line, ':');
            if (colon != NULL) {
                uint16_t data_len;
                /* 取出 +IPD 声明的长度 */
                if (sscanf(line, "+IPD,%hu", &data_len) == 1) {
                    char *payload = colon + 1;
                    uint16_t actual = (uint16_t)strlen(payload);
                    /* 以实际行内长度为准 (不信任 data_len, 防越界) */
                    if (actual > 0 && g_rx_cb != NULL) {
                        g_rx_cb((const uint8_t *)payload, actual);
                    }
                }
            }
            continue;
        }

        /* --- WIFI 异步事件 --- */
        if (strncmp(line, "WIFI ", 5) == 0) {
            if (strstr(line, "GOT IP")) {
                g_state = ESP_STA_WIFI_OK;
            } else if (strstr(line, "DISCONNECT")) {
                if (g_state >= ESP_STA_WIFI_OK) {
                    g_state = ESP_STA_READY;
                }
            }
            /* CONNECTED 事件只记日志, 不等它 */
            continue;
        }

        /* --- TCP 异步事件 --- */
        if (strncmp(line, "CLOSED", 6) == 0) {
            if (g_state >= ESP_STA_TCP_CONNECT) {
                g_state = ESP_STA_WIFI_OK;
            }
            continue;
        }
        if (strncmp(line, "SEND OK", 7) == 0
         || strncmp(line, "SEND FAIL", 9) == 0) {
            continue;   /* SEND 状态不算应答终态 */
        }
        if (strstr(line, "busy") != NULL
         || strstr(line, "Busy") != NULL) {
            continue;
        }
        /* 纯数字 (可能 CIPSEND 的长度回显) */
        {
            uint8_t  all_digit = 1;
            char    *p = line;
            while (*p) { if (*p < '0' || *p > '9') { all_digit = 0; break; } p++; }
            if (all_digit && *line) continue;
        }

        /* --- 累积到应答缓冲 --- */
        free_space = ESP_RESP_BUF_SIZE - g_resp_len - 1;
        if (free_space > 2) {
            uint16_t len = (uint16_t)strlen(line);
            if (len > free_space) len = free_space;
            if (g_resp_len > 0) {
                g_resp_buf[g_resp_len++] = '\n';   /* 换行分隔 */
            }
            memcpy(g_resp_buf + g_resp_len, line, len);
            g_resp_len += len;
            g_resp_buf[g_resp_len] = '\0';
        }

        /* --- 检测终态 --- */
        if (strcmp(line, "OK")    == 0
         || strcmp(line, "ERROR") == 0
         || strcmp(line, "FAIL")  == 0) {
            g_resp_done = 1;    /* 由调用者检查并 give 信号量 */
        }
    }
}

/* ============================================================
 * 核心 — AT 命令发送 & 等待应答
 *
 * 流程:
 *   1. flush 旧数据
 *   2. 发送 "cmd\r\n"
 *   3. 轮询 parse_response() + yield, 直到 g_resp_done 或超时
 *   4. 检查应答中是否含 expect 字符串
 *
 * 参数:
 *   cmd       — 命令 (不含 \r\n), NULL 表示只等应答 (如 CIPSEND 数据后)
 *   expect    — 期望响应关键词 (如 "OK" 或 ">"), NULL 默认 "OK"
 *   resp      — [可选] 输出应答文本
 *   resp_len  — resp 缓冲区大小
 *   timeout_ms— 超时 (ms), 0 使用默认
 * ============================================================ */

esp_err_t esp8266_SendCmd(const char *cmd, const char *expect,
                           char *resp, uint16_t resp_len,
                           uint32_t timeout_ms)
{
    TickType_t start;
    uint32_t   to = timeout_ms ? timeout_ms : ESP_CMD_TIMEOUT_MS;

    /* 创建信号量 (首次调用, 初始 taken) */
    if (g_resp_sem == NULL) {
        g_resp_sem = xSemaphoreCreateBinary();
        if (g_resp_sem == NULL) return ESP_ERR_NOMEM;
    }
    /* 确保信号量处于 taken 状态 (清残留) */
    while (xSemaphoreTake(g_resp_sem, 0) == pdTRUE);

    /* 清空旧数据和状态 */
    ring_flush();
    memset(g_resp_buf, 0, sizeof(g_resp_buf));
    g_resp_len  = 0;
    g_resp_done = 0;

    /* 发送命令 + 换行 */
    if (cmd) {
        esp_USART_SendStr(cmd);
    }
    esp_USART_SendStr("\r\n");

    /* 轮询等待应答 */
    start = xTaskGetTickCount();

    for (;;) {
        /* 喂解析器 (处理所有已接收的行) */
        parse_response();

        /* 检查是否收到终态 */
        if (g_resp_done) break;

        /* 超时检查 */
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(to)) {
            return ESP_ERR_TIMEOUT;
        }

        /* yield 给其他任务 (10ms 粒度) */
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* 复制应答给调用者 */
    if (resp && resp_len > 0) {
        strncpy(resp, g_resp_buf, resp_len - 1);
        resp[resp_len - 1] = '\0';
    }

    /* 匹配期望关键词 */
    if (expect) {
        return (strstr(g_resp_buf, expect) != NULL) ? ESP_OK : ESP_ERR_RESP;
    }

    /* 默认: 含 OK 即成功 */
    if (strstr(g_resp_buf, "OK") != NULL)    return ESP_OK;
    if (strstr(g_resp_buf, "ERROR") != NULL
     || strstr(g_resp_buf, "FAIL")  != NULL) return ESP_ERR_RESP;

    return ESP_OK;
}

/* ============================================================
 * 公开 API 实现
 * ============================================================ */

/* --- 初始化模块 --- */
esp_err_t esp8266_Init(uint32_t baud)
{
    uint8_t retry;

    if (g_state != ESP_STA_IDLE) {
        return ESP_ERR_STATE;   /* 已初始化 */
    }

    /* USART2 硬件 */
    esp_USART_Init(baud ? baud : 115200);

    /* 信号量 (if not already) */
    if (g_resp_sem == NULL) {
        g_resp_sem = xSemaphoreCreateBinary();
        if (g_resp_sem == NULL) return ESP_ERR_NOMEM;
    }

    g_state = ESP_STA_READY;

    /* 等待模块上电稳定 (1s) */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* AT 连通性测试，最多重试 3 次 */
    for (retry = 0; retry < 3; retry++) {
        if (esp8266_ATTest() == ESP_OK) {
            /* 基础配置: 关回显 + 单连接 */
            esp8266_SendCmd("ATE0",      "OK", NULL, 0, 2000);
            esp8266_SendCmd("AT+CIPMUX=0", "OK", NULL, 0, 2000);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    g_state = ESP_STA_ERROR;
    return ESP_ERR_HW;
}

/* --- 软复位 --- */
esp_err_t esp8266_Reset(void)
{
    esp_err_t err;

    err = esp8266_SendCmd("AT+RST", "OK", NULL, 0, 5000);
    if (err != ESP_OK) return err;

    /* 等待重启 (~3s) */
    vTaskDelay(pdMS_TO_TICKS(3000));

    ring_flush();
    g_state = ESP_STA_READY;

    /* 重新配置 */
    esp8266_SendCmd("ATE0",      "OK", NULL, 0, 2000);
    esp8266_SendCmd("AT+CIPMUX=0", "OK", NULL, 0, 2000);

    return ESP_OK;
}

/* --- 恢复出厂 --- */
esp_err_t esp8266_Restore(void)
{
    esp_err_t err = esp8266_SendCmd("AT+RESTORE", "OK", NULL, 0, 5000);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        ring_flush();
        g_state = ESP_STA_READY;
    }
    return err;
}

/* --- AT 连通测试 --- */
esp_err_t esp8266_ATTest(void)
{
    return esp8266_SendCmd("AT", "OK", NULL, 0, 2000);
}

/* --- 读取固件版本 --- */
esp_err_t esp8266_GetVersion(char *ver, uint8_t maxlen)
{
    return esp8266_SendCmd("AT+GMR", "OK", ver, maxlen, 3000);
}

/* --- 设置模式 (1=STA, 2=AP, 3=STA+AP) --- */
esp_err_t esp8266_SetMode(uint8_t mode)
{
    char cmd[20];
    sprintf(cmd, "AT+CWMODE=%u", (unsigned)mode);
    return esp8266_SendCmd(cmd, "OK", NULL, 0, 2000);
}

/* --- 连接 WiFi AP --- */
esp_err_t esp8266_ConnectAP(const char *ssid, const char *pwd,
                            uint32_t timeout_ms)
{
    esp_err_t err;
    char cmd[164];
    uint32_t to = timeout_ms ? timeout_ms : ESP_CONNECT_TIMEOUT_MS;

    if (g_state < ESP_STA_READY) return ESP_ERR_STATE;
    if (ssid == NULL || ssid[0] == '\0') return ESP_ERR_WIFI;

    /* 确保 STA 模式 */
    err = esp8266_SetMode(1);
    if (err != ESP_OK) return err;

    /* 若已连其它 AP, 先断开 */
    if (g_state >= ESP_STA_WIFI_OK) {
        esp8266_SendCmd("AT+CWQAP", "OK", NULL, 0, 3000);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    g_state = ESP_STA_CONNECTING;

    /* CWJAP */
    if (pwd && pwd[0] != '\0') {
        sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);
    } else {
        sprintf(cmd, "AT+CWJAP=\"%s\",\"\"", ssid);
    }

    err = esp8266_SendCmd(cmd, "OK", NULL, 0, to);
    if (err == ESP_OK) {
        /* 等 DHCP → WIFI GOT IP */
        TickType_t t0 = xTaskGetTickCount();
        while (g_state != ESP_STA_WIFI_OK) {
            parse_response();   /* 处理异步事件 */
            vTaskDelay(pdMS_TO_TICKS(200));
            if ((xTaskGetTickCount() - t0) >= pdMS_TO_TICKS(to)) {
                g_state = ESP_STA_READY;
                return ESP_ERR_TIMEOUT;
            }
        }
    } else {
        g_state = ESP_STA_READY;
    }

    return err;
}

/* --- 断开 WiFi --- */
esp_err_t esp8266_DisconnectAP(void)
{
    esp_err_t err = esp8266_SendCmd("AT+CWQAP", "OK", NULL, 0, 3000);
    if (err == ESP_OK) {
        g_state = ESP_STA_READY;
    }
    return err;
}

/* --- 获取本机 IP --- */
esp_err_t esp8266_GetLocalIP(char *ip, uint8_t maxlen)
{
    return esp8266_SendCmd("AT+CIFSR", "OK", ip, maxlen, 2000);
}

/* --- 建立 TCP 连接 --- */
esp_err_t esp8266_TCPConnect(const char *remote_ip,
                             uint16_t remote_port,
                             uint32_t timeout_ms)
{
    char cmd[80];
    uint32_t to = timeout_ms ? timeout_ms : ESP_CONNECT_TIMEOUT_MS;

    if (g_state < ESP_STA_WIFI_OK) return ESP_ERR_STATE;
    if (remote_ip == NULL)         return ESP_ERR_TCP;

    /* 若有残留 TCP, 先关 */
    if (g_state >= ESP_STA_TCP_CONNECT) {
        esp8266_TCPClose();
    }

    g_state = ESP_STA_TCP_CONNECT;
    sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%u", remote_ip, (unsigned)remote_port);

    if (esp8266_SendCmd(cmd, "OK", NULL, 0, to) == ESP_OK) {
        g_state = ESP_STA_TCP_OK;
        return ESP_OK;
    }

    g_state = ESP_STA_WIFI_OK;
    return ESP_ERR_TCP;
}

/* --- 关闭 TCP --- */
esp_err_t esp8266_TCPClose(void)
{
    if (g_state < ESP_STA_TCP_CONNECT) return ESP_OK;

    esp_err_t err = esp8266_SendCmd("AT+CIPCLOSE", "OK", NULL, 0, 3000);
    if (err == ESP_OK) {
        g_state = ESP_STA_WIFI_OK;
    }
    return err;
}

/* --- TCP 发送数据 --- */
esp_err_t esp8266_TCPSend(const uint8_t *data, uint16_t len)
{
    char cmd[16];

    if (g_state != ESP_STA_TCP_OK) return ESP_ERR_STATE;
    if (data == NULL || len == 0)  return ESP_ERR_SEND;

    /* 1. 声明长度, 等待 ">" */
    sprintf(cmd, "AT+CIPSEND=%u", (unsigned)len);
    if (esp8266_SendCmd(cmd, ">", NULL, 0, 5000) != ESP_OK) {
        return ESP_ERR_SEND;
    }

    /* 2. 收到 ">" -> 发送数据负载 (不带 \r\n) */
    esp_USART_SendBuf(data, len);

    /* 3. 等待 SEND OK */
    return esp8266_SendCmd(NULL, "SEND OK", NULL, 0, 5000);
}

/* --- TCP 发送字符串 (便捷) --- */
esp_err_t esp8266_TCPSendString(const char *str)
{
    if (str == NULL) return ESP_ERR_SEND;
    return esp8266_TCPSend((const uint8_t *)str, (uint16_t)strlen(str));
}

/* --- 注册 TCP 数据接收回调 --- */
void esp8266_SetRxCallback(esp_rx_callback_t cb)
{
    g_rx_cb = cb;
}

/* --- 查询连接状态 --- */
esp_sta_t esp8266_GetState(void)
{
    return g_state;
}

/* --- TCP 是否就绪 (1=可收发) --- */
uint8_t esp8266_IsConnected(void)
{
    return (g_state == ESP_STA_TCP_OK) ? 1 : 0;
}
