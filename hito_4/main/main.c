#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "drivers/adc_driver.h"
#include "sensors/ldr_sensor.h"
#include "esp_log.h"

#define TAG "MAIN"

void app_main(void) {
  adc_continuous_handle_t adc_handle;
  adc_driver_init(&adc_handle);

  ldr_init(adc_handle);

  while (1) {
    uint8_t light = ldr_get_light_level();

    ESP_LOGI(TAG, "Luz = %d%%", light);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

