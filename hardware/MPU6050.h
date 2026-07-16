#ifndef __MPU_H
#define __MPU_H

void mpu_wreg(uint8_t regadress,uint8_t data);

uint8_t mpu_rreg(uint8_t regadress);


int mpu_Init(void);

void mpu6050_getdata(int16_t *AccX, int16_t*AccY, int16_t*AccZ, 
						int16_t* GyroX, int16_t*GyroY, int16_t*GyroZ);
#endif
