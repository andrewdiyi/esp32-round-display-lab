#include "I2C_Driver.h"
void I2C_Init() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
  Wire.setTimeOut(30);
}
// Preserve the vendor convention: false means success.
bool I2C_Read(uint8_t addr, uint8_t reg, uint8_t *data, uint32_t count) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0)
    return true;
  if (Wire.requestFrom(addr, count) != count)
    return true;
  for (uint32_t i = 0; i < count; i++)
    data[i] = Wire.read();
  return false;
}
bool I2C_Write(uint8_t addr, uint8_t reg, const uint8_t *data, uint32_t count) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(data, count);
  return Wire.endTransmission(true) != 0;
}
