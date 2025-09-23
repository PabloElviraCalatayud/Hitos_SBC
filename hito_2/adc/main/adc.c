#include <stdio.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_adc/adc_continuous.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define TAG "ADC_CONT"
#define BUF_SIZE 1024
#define FRAME_SIZE 256
#define SAMPLE_FREQUENCY_HZ 20000

#define VCC 3.3
#define VCC_MAX_VALUE 4095.0
#define FIXED_RESISTOR 10000.0 // 10kΩ reference resistor

#define SEG_A_1 6
#define SEG_B_1 7
#define SEG_C_1 8
#define SEG_D_1 9

#define SEG_A_2 18
#define SEG_B_2 19
#define SEG_C_2 21
#define SEG_D_2 22

#define NUMBER_OF_DISPLAYS 2
#define SEGMENTS_PER_DISPLAY 4


gpio_num_t seg_pins[SEGMENTS_PER_DISPLAY] = {
	SEG_A_1,
	SEG_B_1,
	SEG_C_1,
	SEG_D_1,
  SEG_A_2,
  SEG_B_2,
  SEG_C_2,
  SEG_D_2
};
gpio_num_t dig_common_pins[NUMBER_OF_DISPLAYS] = {
	DIG_COMMON_1,
	DIG_COMMON_2,
	DIG_COMMON_3,
	DIG_COMMON_4
};

static uint8_t display_buffer[NUMBER_OF_DISPLAYS][SEGMENTS_PER_DISPLAY];

adc_continuous_handle_t adc_config(void);
uint16_t adc_read(adc_continuous_handle_t);
float calculate_resistance(uint16_t);

void configure_segment_display();

void app_main(void) {
    adc_continuous_handle_t adc_handle = adc_config();

    while (1) {
        uint16_t raw_value = adc_read(adc_handle);
        float resistance = calculate_resistance(raw_value);
	resistance /= 1000;
        ESP_LOGI(TAG, "Average resistance: %.2fK", resistance);
        float adc_voltage = ((float)raw_value / VCC_MAX_VALUE) * VCC;
        //ESP_LOGI(TAG, "Raw: %d, Voltage: %.3f V", raw_value, adc_voltage);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

adc_continuous_handle_t adc_config(void) {
    adc_continuous_handle_t handle = NULL;

    adc_continuous_handle_cfg_t adc_handle_cfg = {
        .max_store_buf_size = BUF_SIZE,
        .conv_frame_size = FRAME_SIZE,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_handle_cfg, &handle));

    static adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_12,          
        .channel = ADC_CHANNEL_0,          
	.unit = ADC_UNIT_1,                
        .bit_width = ADC_BITWIDTH_12,      
    };

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = SAMPLE_FREQUENCY_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
        .pattern_num = 1,
        .adc_pattern = &adc_pattern,
    };

    ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));

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
    const float adc_max = VCC_MAX_VALUE; // 4095.0
    const float vcc = VCC;               // 3.3

    if (raw_value > (uint16_t)adc_max){
        return -1.0f;
    }


    float v_adc = ((float)raw_value / adc_max) * vcc;

    if (v_adc <= 0.0f) {
        return 0.0f; // luz muy intensa → resistencia ≈ 0
    }
    if (v_adc >= vcc) {
        return -1.0f; // oscuridad total → resistencia → ∞
  }
    // Fórmula para pull-up
    return FIXED_RESISTOR * (v_adc / (vcc - v_adc));
}

void configure_segment_display(){
	for(int i = 0; i < SEGMENTS_PER_DISPLAY; i++){
		gpio_reset_pin(seg_pins[i]);
		gpio_set_direction(seg_pins[i],GPIO_MODE_OUTPUT);
	}
	for(int i = 0; i < NUMBER_OF_DISPLAYS; i++){
		gpio_reset_pin(dig_common_pins[i]);
		gpio_set_direction(dig_common_pins[i],GPIO_MODE_OUTPUT);
	}
}

