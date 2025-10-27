#include "network/wifi_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void) {
  wifi_manager_init();

  ESP_LOGI("MAIN", "Esperando conexión WiFi...");
  while (!wifi_manager_is_connected()) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  ESP_LOGI("MAIN", "WiFi conectado correctamente.");

  // Aquí ya tienes OTA activa desde wifi_manager
  while (true) {
    ESP_LOGI("MAIN", "Sistema operativo normalmente...");
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

