/**
 ******************************************************************************
 * @file    protocol.c
 * @brief   MAX RTOS 二进制帧协议 — STM32 端实现
 *
 * 与上位机 d:\max_rtos_host\protocol.py 完全保持二进制兼容。
 ******************************************************************************
 */

#include "protocol.h"
#include <string.h>

/* ============================================================
 * 底层 — 打包任意类型帧
 * ============================================================ */

uint8_t proto_pack(uint8_t pkt_type, uint8_t seq,
                   const uint8_t *payload, uint8_t len,
                   uint8_t *out)
{
    uint8_t i;
    uint8_t check;
    uint8_t total = FRAME_OVERHEAD + len;

    /* 帧头 */
    out[0] = FRAME_HEADER;
    out[1] = pkt_type;
    out[2] = len;
    out[3] = seq;

    /* Payload (memcpy 比逐字节复制快, 且 MDK 会内联小长度) */
    if (len > 0 && payload != NULL) {
        memcpy(out + 4, payload, len);
    }

    /* XOR 校验: TYPE ^ LEN ^ SEQ ^ PAYLOAD[0..LEN-1] */
    check = pkt_type ^ len ^ seq;
    for (i = 0; i < len; i++) {
        check ^= payload[i];
    }
    out[4 + len] = check;

    /* 帧尾 */
    out[5 + len] = FRAME_FOOTER;

    return total;
}

/* ============================================================
 * 便捷打包 — MPU6050 全量数据
 *
 * Payload: 6 × int16 LE = 12 bytes
 *   字节序: ARM Cortex-M3 是小端, int16 直接写即可
 *   但为保证跨平台兼容, 显式拆分为 [Lo, Hi]
 * ============================================================ */

uint8_t proto_pack_mpu6050(uint8_t seq,
                           int16_t ax, int16_t ay, int16_t az,
                           int16_t gx, int16_t gy, int16_t gz,
                           uint8_t *out)
{
    uint8_t payload[12];

    payload[ 0] = (uint8_t)(ax & 0xFF);
    payload[ 1] = (uint8_t)((ax >> 8) & 0xFF);
    payload[ 2] = (uint8_t)(ay & 0xFF);
    payload[ 3] = (uint8_t)((ay >> 8) & 0xFF);
    payload[ 4] = (uint8_t)(az & 0xFF);
    payload[ 5] = (uint8_t)((az >> 8) & 0xFF);
    payload[ 6] = (uint8_t)(gx & 0xFF);
    payload[ 7] = (uint8_t)((gx >> 8) & 0xFF);
    payload[ 8] = (uint8_t)(gy & 0xFF);
    payload[ 9] = (uint8_t)((gy >> 8) & 0xFF);
    payload[10] = (uint8_t)(gz & 0xFF);
    payload[11] = (uint8_t)((gz >> 8) & 0xFF);

    return proto_pack(PKT_MPU6050, seq, payload, 12, out);
}

/* ============================================================
 * 便捷打包 — 心率 + 血氧
 *
 * Payload: 6 bytes
 *   [0:1]  hr       uint16 LE (bpm)
 *   [2:3]  spo2     uint16 LE  (×10, e.g. 985 = 98.5%)
 *   [4]    hr_valid uint8    (1=有效)
 *   [5]    spo2_valid uint8  (1=有效)
 * ============================================================ */

uint8_t proto_pack_heart_rate(uint8_t seq,
                              uint16_t hr, uint16_t spo2_x10,
                              uint8_t hr_valid, uint8_t spo2_valid,
                              uint8_t *out)
{
    uint8_t payload[6];

    payload[0] = (uint8_t)(hr & 0xFF);
    payload[1] = (uint8_t)((hr >> 8) & 0xFF);
    payload[2] = (uint8_t)(spo2_x10 & 0xFF);
    payload[3] = (uint8_t)((spo2_x10 >> 8) & 0xFF);
    payload[4] = hr_valid;
    payload[5] = spo2_valid;

    return proto_pack(PKT_HEART_RATE, seq, payload, 6, out);
}

/* ============================================================
 * 便捷打包 — 运动分数
 *
 * Payload: 3 bytes
 *   [0:1]  score       uint16 LE (0~1023, 分数越高越剧烈)
 *   [2]    level       uint8    (0=静止,1=微动,2=步行,3=跑步,4=剧烈)
 * ============================================================ */

uint8_t proto_pack_motion(uint8_t seq,
                          uint16_t score, uint8_t level,
                          uint8_t *out)
{
    uint8_t payload[3];

    payload[0] = (uint8_t)(score & 0xFF);
    payload[1] = (uint8_t)((score >> 8) & 0xFF);
    payload[2] = level;

    return proto_pack(PKT_MOTION, seq, payload, 3, out);
}

/* ============================================================
 * 便捷打包 — 文本消息
 *
 * Payload: ASCII 字符串 (变长, 最大 255 bytes)
 * ============================================================ */

uint8_t proto_pack_text(uint8_t seq,
                        const char *text,
                        uint8_t *out)
{
    uint8_t len = (uint8_t)strlen(text);
    if (len > FRAME_MAX_PAYLOAD) {
        len = FRAME_MAX_PAYLOAD;
    }
    return proto_pack(PKT_TEXT, seq, (const uint8_t *)text, len, out);
}

/* ============================================================
 * 便捷打包 — 心跳帧
 *
 * Payload: 0 bytes
 * ============================================================ */

uint8_t proto_pack_heartbeat(uint8_t seq, uint8_t *out)
{
    return proto_pack(PKT_HEARTBEAT, seq, NULL, 0, out);
}
