/**
 ******************************************************************************
 * @file    MPU6050.c
 * @brief   MPU6050 6轴传感器驱动 — 软件 I2C 版本 (PB10=SCL, PB11=SDA)
 *
 * 使用 hardware/I2C.c 的软件模拟 I2C, 不依赖 STM32 硬件 I2C 外设。
 * 已确认: 此 I2C 在 MAX30102+OLED 同一系统中稳定工作。
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "Delay.h"
#include "I2C.h"

#define mpu_adress    0xD0          /* 7bit=0x68 << 1, AD0=GND */
#include "mpureg.h"

/* ============================================================
 * 底层 — 软件 I2C 读写
 * ============================================================ */

void mpu_wreg(uint8_t regadress, uint8_t data)
{
    i2c_start();
    i2c_sendbyte(mpu_adress);       /* 写地址 */
    i2c_receiveack();
    i2c_sendbyte(regadress);        /* 寄存器 */
    i2c_receiveack();
    i2c_sendbyte(data);             /* 数据 */
    i2c_receiveack();
    i2c_stop();
}

uint8_t mpu_rreg(uint8_t regadress)
{
    uint8_t data;

    /* 先写寄存器地址 */
    i2c_start();
    i2c_sendbyte(mpu_adress);
    i2c_receiveack();
    i2c_sendbyte(regadress);
    i2c_receiveack();

    /* 重复 START → 读 */
    i2c_start();
    i2c_sendbyte(mpu_adress | 0x01);  /* 读地址 */
    i2c_receiveack();
    data = i2c_receive();
    i2c_sendack(1);                    /* NACK (单字节) */
    i2c_stop();

    return data;
}

/*
 * 突发读 — 与 mpu_rreg 逻辑相同, 但连续读 len 字节,
 * 最后 1 字节发 NACK, 之前发 ACK。
 */
static void mpu_read_len(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    /* 写寄存器地址 */
    i2c_start();
    i2c_sendbyte(mpu_adress);
    i2c_receiveack();
    i2c_sendbyte(reg);
    i2c_receiveack();

    /* 重复 START → 连续读 */
    i2c_start();
    i2c_sendbyte(mpu_adress | 0x01);
    i2c_receiveack();

    for (i = 0; i < len; i++) {
        buf[i] = i2c_receive();
        if (i == len - 1)
            i2c_sendack(1);           /* 末字节: NACK */
        else
            i2c_sendack(0);           /* 非末字节: ACK  */
    }
    i2c_stop();
}

/* ============================================================
 * 初始化 & 数据获取
 * ============================================================ */

int mpu_Init(void)
{
    uint8_t whoami;

    /* 软件 I2C 初始化 (PB10=SCL, PB11=SDA, 开漏) */
    i2c_Init();

    /* ---- MPU6050 寄存器配置 ---- */

    /* 1. 复位 */
    mpu_wreg(MPU6050_PWR_MGMT_1, 0x80);
    Delay_ms(100);

    /* 2. 唤醒 (内部振荡器) */
    mpu_wreg(MPU6050_PWR_MGMT_1, 0x00);
    Delay_ms(10);

    /* 3. 所有传感器上电 */
    mpu_wreg(MPU6050_PWR_MGMT_2, 0x00);

    /* 4. 采样率: 1kHz/(1+9) = 100Hz */
    mpu_wreg(MPU6050_SMPLRT_DIV, 0x09);

    /* 5. DLPF_CFG=3 (accel 44Hz / gyro 42Hz) */
    mpu_wreg(MPU6050_CONFIG, 0x03);

    /* 6. 陀螺仪 ±2000 °/s */
    mpu_wreg(MPU6050_GYRO_CONFIG, 0x18);

    /* 7. 加速度 ±2g */
    mpu_wreg(MPU6050_ACCEL_CONFIG, 0x00);

    /* 8. 自检: WHO_AM_I 支持 MPU6050(0x68) 和 MPU6500(0x70) */
    whoami = mpu_rreg(MPU6050_WHO_AM_I);
    if (whoami == 0x68 || whoami == 0x70) {
        return 0;  /* OK */
    } else {
        return -1; /* FAIL */
    }
}

void mpu6050_getdata(int16_t *AccX, int16_t *AccY, int16_t *AccZ,
                     int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ)
{
    uint8_t buf[14];

    /* 突发读 14 字节 */
    mpu_read_len(MPU6050_ACCEL_XOUT_H, buf, 14);

    *AccX  = (int16_t)(((uint16_t)buf[ 0] << 8) | buf[ 1]);
    *AccY  = (int16_t)(((uint16_t)buf[ 2] << 8) | buf[ 3]);
    *AccZ  = (int16_t)(((uint16_t)buf[ 4] << 8) | buf[ 5]);
    /* buf[6:7] = 温度 (跳过) */
    *GyroX = (int16_t)(((uint16_t)buf[ 8] << 8) | buf[ 9]);
    *GyroY = (int16_t)(((uint16_t)buf[10] << 8) | buf[11]);
    *GyroZ = (int16_t)(((uint16_t)buf[12] << 8) | buf[13]);
}
