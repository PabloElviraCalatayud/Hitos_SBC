#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_continuous.h"
#include "ldr_monitor.h"
#include "ssd1306.h"

#define TAG "LDR_MONITOR"
#define BUF_SIZE 1024
#define FRAME_SIZE 256
#define SAMPLE_FREQUENCY_HZ 20000

#define VCC 3.3
#define VCC_MAX_VALUE 4095.0
#define FIXED_RESISTOR 10000.0 // 10kΩ

// Pines display 7 segmentos BCD
#define SEG_A_1 2
#define SEG_B_1 4
#define SEG_C_1 5
#define SEG_D_1 12
#define SEG_A_2 18
#define SEG_B_2 19
#define SEG_C_2 21
#define SEG_D_2 22

#define BCD_WEIGHTS 4
#define N 2 // número de dígitos

// OLED I2C
#define I2C_MASTER_SCL_IO 23
#define I2C_MASTER_SDA_IO 25
#define OLED_ADDR 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

gpio_num_t pins_display_1[BCD_WEIGHTS] = { SEG_A_1, SEG_B_1, SEG_C_1, SEG_D_1 };
gpio_num_t pins_display_2[BCD_WEIGHTS] = { SEG_A_2, SEG_B_2, SEG_C_2, SEG_D_2 };

static adc_continuous_handle_t adc_config(void);
static uint16_t adc_read(adc_continuous_handle_t);
static float calculate_resistance(uint16_t);
static uint8_t calculate_light_level(float);

static void configure_segment_display(gpio_num_t *);
static void write_bcd_digit(uint8_t, gpio_num_t *);
static void get_digits(uint8_t, uint8_t [N]);

static void init_oled(SSD1306_t *dev);
static void oled_show_light_bar(SSD1306_t *dev, uint8_t level, float resistance);

// ---------------- TASK ----------------
static void ldr_monitor_task(void *pvParameters) {
    adc_continuous_handle_t adc_handle = adc_config();

    configure_segment_display(pins_display_1);
    configure_segment_display(pins_display_2);

    SSD1306_t dev;
    init_oled(&dev);

    while (1) {
        uint16_t raw_value = adc_read(adc_handle);
        float resistance = calculate_resistance(raw_value);

        uint8_t light_level = calculate_light_level(resistance);

        ESP_LOGI(TAG, "Raw: %d, R=%.1f Ohm, Luz=%d",
                 raw_value, resistance, light_level);

        // Displays 7 segmentos
        uint8_t digits[N];
        get_digits(light_level, digits);
        write_bcd_digit(digits[0], pins_display_1);
        write_bcd_digit(digits[1], pins_display_2);

        // OLED
        oled_show_light_bar(&dev, light_level, resistance);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void ldr_monitor_start(void) {
    xTaskCreate(ldr_monitor_task, "ldr_monitor_task", 4096, NULL, 5, NULL);
}

// ---------------- FUNCIONES ----------------
static adc_continuous_handle_t adc_config(void) {
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

static uint16_t adc_read(adc_continuous_handle_t handle) {
    uint8_t buffer[FRAME_SIZE];
    uint32_t out_length = 0;
    uint32_t sum = 0;
    int num_samples = 0;

    esp_err_t ret = adc_continuous_read(handle, buffer, sizeof(buffer),
                                        &out_length, 1000);

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

static float calculate_resistance(uint16_t raw_value) {
    if (raw_value > VCC_MAX_VALUE) return -1.0f;

    float v_adc = ((float)raw_value / VCC_MAX_VALUE) * VCC;
    if (v_adc <= 0.0f) return 0.0f;
    if (v_adc >= VCC)  return -1.0f;

    return FIXED_RESISTOR * (v_adc / (VCC - v_adc));
}

static uint8_t calculate_light_level(float resistance) {
    const float R_DARK = 100000.0f;
    const float R_LIGHT = 1000.0f;

    float level = 100.0f * (log10f(R_DARK / resistance) / log10f(R_DARK / R_LIGHT));

    if (level < 0.0f) level = 0.0f;
    if (level > 99.0f) level = 99.0f;

    return (uint8_t)roundf(level);
}

static void configure_segment_display(gpio_num_t *pins){
    for(int i = 0; i < BCD_WEIGHTS; i++){
        gpio_reset_pin(pins[i]);
        gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
    }
}

static void write_bcd_digit(uint8_t digit, gpio_num_t *pins){
    for (int i = 0; i < 4; i++) {
        int bit = (digit >> i) & 0x01;
        gpio_set_level(pins[i], bit);
    }
}

static void get_digits(uint8_t value, uint8_t digits[N]){
    digits[0] = value / 10;
    digits[1] = value % 10;
}

static void init_oled(SSD1306_t *dev) {
    i2c_master_init(dev, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, -1);
    ssd1306_init(dev, OLED_WIDTH, OLED_HEIGHT);
    ssd1306_clear_screen(dev, false);
    ssd1306_contrast(dev, 0xff);
}

static void oled_show_light_bar(SSD1306_t *dev, uint8_t level, float resistance) {
    ssd1306_clear_screen(dev, false);

    char line0[20];
    snprintf(line0, sizeof(line0), "Luz: %d/99", level);
    ssd1306_display_text(dev, 0, line0, strlen(line0), false);

    char line1[20];
    snprintf(line1, sizeof(line1), "R: %.1f Ohm", resistance);
    ssd1306_display_text(dev, 1, line1, strlen(line1), false);

    char line2[20];
    snprintf(line2, sizeof(line2), "R: %.2f kOhm", resistance/1000.0);
    ssd1306_display_text(dev, 2, line2, strlen(line2), false);

    const int num_blocks = 10;
    int filled_blocks = (level * num_blocks) / 99;

    char bar[num_blocks + 1];
    for (int i = 0; i < num_blocks; i++) {
        bar[i] = (i < filled_blocks) ? '#' : '-';
    }
    bar[num_blocks] = '\0';

    ssd1306_display_text(dev, 4, bar, strlen(bar), false);
}

