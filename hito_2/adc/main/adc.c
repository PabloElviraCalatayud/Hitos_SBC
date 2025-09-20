#include <stdio.h>
#include <stdint.h>   // <- necesario para uint8_t, uint32_t
#include "esp_log.h"
#include "esp_adc/adc_continuous.h"

#define TAG "ADC_CONT"
#define BUF_SIZE 1024
#define FRAME_SIZE 256
#define SAMPLE_FREQUENCY_HZ 20000

adc_continuous_handle_t config_adc(void);

void app_main(void){
    adc_continuous_handle_t adc_handle = config_adc();
    uint8_t result[FRAME_SIZE];
    uint32_t ret_num = 0;

    while (1) {
        esp_err_t ret = adc_continuous_read(adc_handle, result, sizeof(result),
                                            &ret_num, 1000);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Leídos %lu bytes", (unsigned long)ret_num);
        }
    }
}

adc_continuous_handle_t config_adc(void) {
    adc_continuous_handle_t handle = NULL;

    // 1. Crear handle del driver ADC continuo
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = BUF_SIZE,
        .conv_frame_size = FRAME_SIZE,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &handle));

    // 2. Definir el patrón de muestreo
    static adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_12,          // hasta ~3.3V
        .channel = ADC_CHANNEL_0,          // GPIO36
        .unit = ADC_UNIT_1,                // mejor usar ADC1
        .bit_width = ADC_BITWIDTH_DEFAULT, // 12 bits
    };

    // 3. Configuración digital del ADC
    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = SAMPLE_FREQUENCY_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
        .pattern_num = 1,
        .adc_pattern = &adc_pattern,
    };

    ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));

    // 4. Arrancar la conversión
    ESP_ERROR_CHECK(adc_continuous_start(handle));

    return handle;
}

