#include "ldr_sensor.h"
#include "esp_log.h"
#include "utils/math_utils.h"

#define TAG "LDR_SENSOR"
#define FIXED_RESISTOR 10000.0f
#define VCC 3.3f
#define VCC_MAX_VALUE 4095.0f

static adc_channel_result_t ldr_result = {
  .channel = ADC_CHANNEL_0,
  .average = 0
};

void ldr_init(adc_continuous_handle_t handle) {
  ESP_LOGI(TAG, "LDR inicializado (canal ADC0)");
}

float ldr_get_resistance(adc_continuous_handle_t handle) {
  if (!handle) return -1.0f;

  if (adc_driver_read_multi(handle, &ldr_result, 1) <= 0) {
    ESP_LOGW(TAG, "No se pudieron leer datos del ADC");
    return -1.0f;
  }

  uint16_t raw_value = ldr_result.average;
  if (raw_value == 0) return 0.0f;

  float v_adc = ((float)raw_value / VCC_MAX_VALUE) * VCC;
  if (v_adc >= VCC) v_adc = VCC - 0.001f;
  if (v_adc <= 0.0f) v_adc = 0.001f;

  float resistance = FIXED_RESISTOR * (v_adc / (VCC - v_adc));
  return resistance;
}

