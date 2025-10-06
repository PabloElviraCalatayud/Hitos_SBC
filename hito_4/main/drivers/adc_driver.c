#include "adc_driver.h"
#include "esp_log.h"
#include <string.h>

#define TAG "ADC_DRIVER"
#define FRAME_SIZE 1024

static const adc_channel_t adc_channels[] = {
  ADC_CHANNEL_0,
};

esp_err_t adc_driver_init(adc_continuous_handle_t *out_handle) {
  adc_continuous_handle_t handle;
  adc_continuous_handle_cfg_t handle_cfg = {
    .max_store_buf_size = 1024,
    .conv_frame_size = 256,
  };
  ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_cfg, &handle));

  const int num_channels = sizeof(adc_channels) / sizeof(adc_channels[0]);
  adc_digi_pattern_config_t pattern[num_channels];

  for (int i = 0; i < num_channels; i++) {
    pattern[i].atten = ADC_ATTEN_DB_11;
    pattern[i].channel = adc_channels[i];
    pattern[i].unit = ADC_UNIT_1;
    pattern[i].bit_width = ADC_BITWIDTH_12;
  }

  adc_continuous_config_t dig_cfg = {
    .sample_freq_hz = 20000,
    .conv_mode = ADC_CONV_SINGLE_UNIT_1,
    .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
    .pattern_num = num_channels,
    .adc_pattern = pattern,
  };

  ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));
  ESP_ERROR_CHECK(adc_continuous_start(handle));

  *out_handle = handle;
  return ESP_OK;

}

int adc_driver_read_multi(adc_continuous_handle_t handle, adc_channel_result_t *results, int num_channels) {
  uint8_t buffer[FRAME_SIZE];
  uint32_t out_len = 0;
  esp_err_t ret = adc_continuous_read(handle, buffer, sizeof(buffer), &out_len, 1000);
  if (ret != ESP_OK) {
    for (int i = 0; i < num_channels; i++) {
      results[i].average = 0;
    }
    ESP_LOGW(TAG, "No ADC data read (%s)", esp_err_to_name(ret));

    return 0;
  }

  uint64_t sum[16] = {0};
  uint32_t count[16] = {0};
  size_t result_size = sizeof(adc_digi_output_data_t);

  for (uint32_t i = 0; i + result_size <= out_len; i += result_size) {
    adc_digi_output_data_t sample;
    memcpy(&sample, &buffer[i], result_size);
    uint8_t ch = sample.type1.channel;
    sum[ch] += sample.type1.data;
    count[ch]++;
  }

  for (int i = 0; i < num_channels; i++) {
    uint8_t ch = results[i].channel;
    results[i].average = (count[ch] > 0) ? (sum[ch] / count[ch]) : 0;
  }

  return out_len / result_size;
}

