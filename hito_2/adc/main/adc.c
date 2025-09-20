#include <stdio.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_adc/adc_continuous.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "ADC_CONT"
#define BUF_SIZE 1024
#define FRAME_SIZE 256
#define SAMPLE_FREQUENCY_HZ 20000

#define VCC 3.3
#define VCC_MAX_VALUE 4095.0
#define FIXED_RESISTOR 10000.0 // 10kΩ reference resistor

adc_continuous_handle_t adc_config(void);
uint16_t adc_read(adc_continuous_handle_t);
float calculate_resistance(uint16_t);

void app_main(void) {
    adc_continuous_handle_t adc_handle = adc_config();

    while (1) {
        uint16_t raw_value = adc_read(adc_handle);
        float resistance = calculate_resistance(raw_value);
	resistance /= 1000;
        ESP_LOGI(TAG, "Average resistance: %.2fK", resistance);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

adc_continuous_handle_t adc_config(void) {
    adc_continuous_handle_t handle = NULL;

    // 1. Create ADC continuous handle
    adc_continuous_handle_cfg_t adc_handle_cfg = {
        .max_store_buf_size = BUF_SIZE,
        .conv_frame_size = FRAME_SIZE,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_handle_cfg, &handle));

    // 2. Define sampling pattern
    static adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_12,          // up to ~3.3V
        .channel = ADC_CHANNEL_0,          // GPIO36
        .unit = ADC_UNIT_1,                // use ADC1
        .bit_width = ADC_BITWIDTH_12,      // 12 bits
    };

    // 3. Digital ADC configuration
    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = SAMPLE_FREQUENCY_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
        .pattern_num = 1,
        .adc_pattern = &adc_pattern,
    };

    ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));

    // 4. Start conversion
    ESP_ERROR_CHECK(adc_continuous_start(handle));

    return handle;
}

uint16_t adc_read(adc_continuous_handle_t handle) {
    uint8_t buffer[FRAME_SIZE];
    uint32_t out_length = 0;
    uint32_t timeout_ms = 1000;
    uint32_t sum = 0;
    int num_samples = 0;

    esp_err_t ret = adc_continuous_read(handle, buffer, sizeof(buffer),
                                        &out_length, timeout_ms);

    if (ret == ESP_OK) {
        num_samples = out_length / sizeof(adc_digi_output_data_t);

        for (int i = 0; i < out_length; i += sizeof(adc_digi_output_data_t)) {
            adc_digi_output_data_t *p = (adc_digi_output_data_t *)&buffer[i];
            uint16_t raw_value = p->type1.data;
            sum += raw_value;
        }

        return (num_samples > 0) ? (sum / num_samples) : 0;
    } else {
        ESP_LOGW(TAG, "No ADC data read (%s)", esp_err_to_name(ret));
        return 0;
    }
}

float calculate_resistance(uint16_t raw_value) {
    float adc_voltage = ((float)raw_value / VCC_MAX_VALUE) * VCC;
    if (adc_voltage <= 0.0f) {
        return -1.0f; // error
    }
    float resistance = FIXED_RESISTOR * ((VCC / adc_voltage) - 1.0f);
    return resistance;
}

