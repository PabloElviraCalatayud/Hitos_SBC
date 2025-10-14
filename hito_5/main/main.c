#include <stdio.h>
#include "network/wifi_manager.h"
#include "network/mqtt_manager.h"
#include "drivers/adc_driver.h"
#include "sensors/ldr_sensor.h"
// #include "sensors/dht_sensor.h"
#include "utils/math_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "esp_system.h"

#define MQTT_BROKER "mqtt://192.168.1.89:1885"
#define MQTT_TOKEN  "5oaq3wkp4wjarfsp90te"

static const char *TAG = "MAIN";

static int g_sensor_interval_ms = 5000; // por defecto 5s

// RPC callback: manejamos setInterval y forzar envío
static void handle_rpc(const char *method, const char *params) {
  ESP_LOGI(TAG, "RPC recibido: method=%s params=%s", method, params);
  if (strcmp(method, "setInterval") == 0) {
    int interval = 0;
    if (sscanf(params, "{\"interval\":%d", &interval) == 1 && interval > 0) {
      g_sensor_interval_ms = interval;
      ESP_LOGI(TAG, "Interval cambiado via RPC a %d ms", g_sensor_interval_ms);
    } else {
      ESP_LOGW(TAG, "RPC setInterval con params invalidos");
    }
  } else if (strcmp(method, "forceSend") == 0) {
    ESP_LOGI(TAG, "Se solicitó envio forzado (forceSend)");
  } else {
    ESP_LOGW(TAG, "RPC desconocido: %s", method);
  }
}

// Entra en light sleep, deteniendo ADC, WiFi y MQTT de forma segura
static void enter_light_sleep_ms(uint32_t ms, adc_continuous_handle_t adc_handle) {
  ESP_LOGI(TAG, "Preparando para entrar en light sleep %d ms...", ms);

  // 🔹 Paso 1: detener ADC para evitar acceso DMA a caché
  if (adc_handle) {
    adc_continuous_stop(adc_handle);
    ESP_LOGI(TAG, "ADC detenido antes de dormir");
  }

  // 🔹 Paso 2: desconectar MQTT
  mqtt_manager_disconnect();
  ESP_LOGI(TAG, "MQTT desconectado");

  // 🔹 Paso 3: detener WiFi con retardo para evitar escritura en NVS
  esp_wifi_stop();
  vTaskDelay(pdMS_TO_TICKS(100)); // espera breve para seguridad
  ESP_LOGI(TAG, "WiFi detenido, entrando en light sleep...");

  // 🔹 Paso 4: entrar en light sleep
  esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
  esp_light_sleep_start();

  // 🔹 Paso 5: al despertar
  ESP_LOGI(TAG, "Despertando de light sleep");

  // 🔹 Paso 6: reiniciar WiFi
  esp_wifi_start();
  ESP_LOGI(TAG, "Esperando reconexión WiFi...");
  while (!wifi_manager_is_connected()) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  ESP_LOGI(TAG, "WiFi reconectado tras light sleep");

  // 🔹 Paso 7: reiniciar ADC
  if (adc_handle) {
    adc_continuous_start(adc_handle);
    ESP_LOGI(TAG, "ADC reiniciado tras light sleep");
  }

  // 🔹 Paso 8: reconectar MQTT
  mqtt_manager_reconnect();
  ESP_LOGI(TAG, "MQTT reconectado tras light sleep");
}
void app_main(void) {
  wifi_manager_init();

  ESP_LOGI(TAG, "Esperando conexión WiFi...");
  while (!wifi_manager_is_connected()) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  ESP_LOGI(TAG, "WiFi conectado.");

  // Esperar 2 segundos para estabilidad post arranque
  vTaskDelay(pdMS_TO_TICKS(2000));

  mqtt_manager_init(MQTT_BROKER, MQTT_TOKEN);
  mqtt_manager_set_rpc_callback(handle_rpc);

  adc_continuous_handle_t adc_handle;
  if (adc_driver_init(&adc_handle) != ESP_OK) {
    ESP_LOGE(TAG, "Error inicializando ADC");
    return;
  }
  ldr_init(adc_handle);

  // // Inicializar DHT11 (actualmente no en uso)
  // dht_sensor_init(GPIO_NUM_4);

  while (true) {
    // LDR
    float resistance = ldr_get_resistance(adc_handle);
    if (resistance >= 0) {
      uint8_t light_level = calculate_light_level(resistance);
      mqtt_manager_publish_light(light_level);
      ESP_LOGI(TAG, "Resistencia: %.2f Ω, Luz: %d%%", resistance, light_level);
    }

    // // DHT11 (comentado por ahora)
    // dht_data_t dht;
    // if (dht_sensor_read(&dht) == ESP_OK) {
    //   mqtt_manager_publish_env(dht.temperature, dht.humidity);
    //   ESP_LOGI(TAG, "DHT -> Temp: %.1f C Hum: %.1f %%", dht.temperature, dht.humidity);
    // } else {
    //   ESP_LOGW(TAG, "Fallo lectura DHT");
    // }

    // Ahorro energético con reconexión segura
    enter_light_sleep_ms((uint32_t)g_sensor_interval_ms,adc_handle);
  }
}

