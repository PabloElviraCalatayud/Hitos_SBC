#pragma once
#include "esp_adc/adc_continuous.h"
#include "esp_err.h"
#include <stdint.h>

typedef struct {
  uint8_t channel;
  uint16_t average;
} adc_channel_result_t;

esp_err_t adc_driver_init(adc_continuous_handle_t *out_handle);
int adc_driver_read_multi(adc_continuous_handle_t handle,adc_channel_result_t *results,int num_channels);
