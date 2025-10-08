#include <stdio.h>
#include "network/wifi_manager.h"
#include "network/mqtt_manager.h"
#include "drivers/adc_driver.h"
#include "sensors/ldr_sensor.h"
#include "utils/math_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define MQTT_BROKER "mqtt://192.168.1.89:1885"
#define MQTT_TOKEN  "udnencgtn1l728eh17f3"

void app_main(void) {
  wifi_manager_init();

  // Esperar hasta que se conecte a la red WiFi
  ESP_LOGI("MAIN", "Esperando conexión WiFi...");
  while (!wifi_manager_is_connected()) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  ESP_LOGI("MAIN", "WiFi conectado correctamente.");

  // Inicializar MQTT solo cuando ya hay conexión WiFi
  mqtt_manager_init(MQTT_BROKER, MQTT_TOKEN);

  adc_continuous_handle_t adc_handle;
  if (adc_driver_init(&adc_handle) != ESP_OK) {
    ESP_LOGE("MAIN", "Error inicializando ADC");
    return;
  }

  ldr_init(adc_handle);

  while (true) {
    float resistance = ldr_get_resistance(adc_handle);
    if (resistance < 0) continue;

    uint8_t light_level = calculate_light_level(resistance);
    mqtt_manager_publish_light(light_level);

    ESP_LOGI("MAIN", "Resistencia: %.2f Ω, Luz: %d%%", resistance, light_level);
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

