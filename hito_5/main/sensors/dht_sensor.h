#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

typedef struct {
  float temperature;
  float humidity;
} dht_data_t;

esp_err_t dht_sensor_init(gpio_num_t pin);
esp_err_t dht_sensor_read(dht_data_t *data);

