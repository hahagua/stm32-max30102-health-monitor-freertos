/**
 ******************************************************************************
 * @file    protocol.h
 * @brief   MAX RTOS 二进制帧协议 — STM32 端编码层
 *
 * 帧格式 (总开销 6 bytes):
 * ┌────────┬──────┬──────┬──────────┬───────────┬────────┬────────┐
 * │ 0xAA   │ TYPE │ LEN  │ SEQ_NUM  │ PAYLOAD   │ CHECK  │ 0x55   │
 * │ 1 byte │ 1 B  │ 1 B  │  1 byte  │ LEN bytes │ 1 byte │ 1 byte │
 * └────────┴──────┴──────┴──────────┴───────────┴────────┴────────┘
 *
 * CHECK = XOR(TYPE, LEN, SEQ_NUM, PAYLOAD[0..LEN-1])
 * 最大 payload: 255 bytes
 *
 * 与上位机 d:\max_rtos_host\protocol.py 保持同步
 ******************************************************************************
 */

#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include "stm32f10x.h"

/* ============================================================
 * 帧协议常量
 * ============================================================ */
#define FRAME_HEADER         0xAA
#define FRAME_FOOTER         0x55
#define FRAME_MAX_PAYLOAD    255
#define FRAME_OVERHEAD       6          /* HEADER+TYPE+LEN+SEQ+CHECK+FOOTER */
#define FRAME_BUF_SIZE       (FRAME_MAX_PAYLOAD + FRAME_OVERHEAD)

/* ============================================================
 * 包类型码 (与上位机 config.py 保持一致)
 * ============================================================ */
#define PKT_MPU6050          0x01       /* 加速度+陀螺仪全量, 12 bytes     */
#define PKT_HEART_RATE       0x02       /* 心率+血氧, 6 bytes              */
#define PKT_MOTION           0x03       /* 运动分数, 3 bytes               */
#define PKT_TEXT             0x04       /* 文本消息, 变长                  */
#define PKT_PPG_RAW          0x05       /* 原始 PPG 波形, 变长             */
#define PKT_HEARTBEAT        0xF0       /* 心跳/Ping, 0 bytes              */

/* ============================================================
 * 公开 API
 * ============================================================ */

/*
 * 底层: 打包任意类型帧到 buffer
 *
 * 参数:
 *   pkt_type  — 包类型 (PKT_MPU6050 / PKT_HEART_RATE / ...)
 *   seq       — 序列号 (0~255, 调用者自行递增回绕)
 *   payload   — 负载数据指针 (可为 NULL 当 len==0)
 *   len       — 负载字节数 (0~255)
 *   out       — 输出缓冲区 (至少 FRAME_BUF_SIZE)
 *
 * 返回: 完整帧的字节数 (6 + len)
 */
uint8_t proto_pack(uint8_t pkt_type, uint8_t seq,
                   const uint8_t *payload, uint8_t len,
                   uint8_t *out);

/*
 * 便捷打包函数 — 每帧内部自动完成 struct 序列化 + proto_pack
 * 返回帧字节数。
 */

/* MPU6050 全量: 6 × int16 LE → 12 bytes */
uint8_t proto_pack_mpu6050(uint8_t seq,
                           int16_t ax, int16_t ay, int16_t az,
                           int16_t gx, int16_t gy, int16_t gz,
                           uint8_t *out);

/* 心率+血氧: HR(uint16) + SpO2(uint16 ×10) + valid flags → 6 bytes */
uint8_t proto_pack_heart_rate(uint8_t seq,
                              uint16_t hr, uint16_t spo2_x10,
                              uint8_t hr_valid, uint8_t spo2_valid,
                              uint8_t *out);

/* 运动分数: score(uint16) + activity_level(uint8) → 3 bytes */
uint8_t proto_pack_motion(uint8_t seq,
                          uint16_t score, uint8_t level,
                          uint8_t *out);

/* 文本消息: ASCII → 变长 */
uint8_t proto_pack_text(uint8_t seq,
                        const char *text,
                        uint8_t *out);

/* 心跳帧: 0 bytes payload */
uint8_t proto_pack_heartbeat(uint8_t seq, uint8_t *out);

#endif /* __PROTOCOL_H */
