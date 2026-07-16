#ifndef __I2C_H
#define __I2C_H


void i2c_Init(void);
void i2c_start(void);
void i2c_stop(void);
void i2c_sendbyte(uint8_t byte);
uint8_t i2c_receive(void);
void i2c_sendack(uint8_t ack);

uint8_t i2c_receiveack(void);

#endif
