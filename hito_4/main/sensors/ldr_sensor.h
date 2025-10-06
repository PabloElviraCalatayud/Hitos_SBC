#pragma once
#include <stdint.h>
#include "esp_adc/adc_continuous.h"

void ldr_init(adc_continuous_handle_t handle);
uint8_t ldr_get_light_level(void);
float ldr_get_resistance(void);

