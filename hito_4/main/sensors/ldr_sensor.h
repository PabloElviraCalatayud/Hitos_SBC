#ifndef LDR_SENSOR_H
#define LDR_SENSOR_H

#include "esp_adc/adc_continuous.h"
#include "drivers/adc_driver.h"

void ldr_init(adc_continuous_handle_t handle);
float ldr_get_resistance(adc_continuous_handle_t handle);

#endif

