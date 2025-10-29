#include "network/wifi_manager.h"
#include "ota/ota_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h" 

void app_main(void) {
  ESP_LOGI("MAIN", "Inicializando NVS...");
  nvs_flash_init();

  if (ota_has_pending_update()) {
    ESP_LOGI("MAIN", "Actualización OTA pendiente detectada. Aplicando...");
    ota_apply_pending_update();
    ESP_LOGI("MAIN", "Reiniciando para arrancar con el nuevo firmware...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
  }

  wifi_manager_init();

  ESP_LOGI("MAIN", "Esperando conexión WiFi...");
  while (!wifi_manager_is_connected()) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  ESP_LOGI("MAIN", "WiFi conectado correctamente.");
  ESP_LOGI("MAIN", "Sistema operativo normalmente...");

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

