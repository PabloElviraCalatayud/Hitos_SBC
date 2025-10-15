#pragma once
#include "driver/i2c.h"
#include "esp_err.h"

#define BME680_I2C_ADDRESS_0  0x76
#define BME680_I2C_ADDRESS_1  0x77

typedef struct {
  float temperature;
  float humidity;
  float pressure;
  float gas_resistance;
} bme680_data_t;

esp_err_t bme680_init(i2c_port_t i2c_port, uint8_t i2c_address);
esp_err_t bme680_read_data(i2c_port_t i2c_port, uint8_t i2c_address, bme680_data_t *data);
void bme680_print_data(const bme680_data_t *data);

