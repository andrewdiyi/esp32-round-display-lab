#pragma once
#include <Wire.h>

constexpr uint8_t I2C_SCL_PIN = 7;
constexpr uint8_t I2C_SDA_PIN = 15;

void I2C_Init();
// The imported vendor drivers expect false on success and true on failure.
bool I2C_Read(uint8_t address, uint8_t reg, uint8_t *data, uint32_t count);
bool I2C_Write(uint8_t address, uint8_t reg, const uint8_t *data, uint32_t count);
