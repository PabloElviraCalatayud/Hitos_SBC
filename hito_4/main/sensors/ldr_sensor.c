#include "ldr_sensor.h"
#include "adc_driver.h"
#include "math_utils.h"
#include "esp_log.h"
#include <math.h>

#define TAG "LDR_SENSOR"
#define ADC_CH_LDR ADC_CHANNEL_0
#define FIXED_RESISTOR 10000.0f
#define VCC 3.3f
#define VCC_MAX_VALUE 4095.0f

static adc_continuous_handle_t adc_handle;
static uint16_t raw_value = 0;

void ldr_init(adc_continuous_handle_t handle) {
  adc_handle = handle;
}

float ldr_get_resistance(void) {
  adc_channel_result_t res = {
    .channel = ADC_CH_LDR
  };
  adc_driver_read_multi(adc_handle, &res, 1);
  raw_value = res.average;
  float v = ((float)raw_value / VCC_MAX_VALUE) * VCC;

  return FIXED_RESISTOR * (v / (VCC - v));
}

uint8_t ldr_get_light_level(void) {
  float resistance = ldr_get_resistance();

  return calculate_light_level(resistance);
}

