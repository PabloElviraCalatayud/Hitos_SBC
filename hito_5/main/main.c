#include <stdio.h>
#include "network/wifi_manager.h"
#include "network/mqtt_manager.h"
#include "drivers/adc_driver.h"
#include "sensors/ldr_sensor.h"
#include "sensors/bme680_sensor.h"
#include "utils/math_utils.h"
#include "utils/telegram_bot.h"
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

// ==================== VARIABLES GLOBALES ====================
static int g_sensor_interval_ms = 2000; // intervalo por defecto 2s

// ==================== RPC ====================
static void handle_rpc(const char *method, const char *params) {
  ESP_LOGI(TAG, "RPC recibido: method=%s params=%s", method, params);
  if (strcmp(method, "setInterval") == 0) {
    int interval = 0;
    if (sscanf(params, "{\"interval\":%d", &interval) == 1 && interval > 0) {
      g_sensor_interval_ms = interval;
      ESP_LOGI(TAG, "Interval cambiado via RPC a %d ms", g_sensor_interval_ms);
    } else {
      ESP_LOGW(TAG, "RPC setInterval con params inválidos");
    }
  } else if (strcmp(method, "forceSend") == 0) {
    ESP_LOGI(TAG, "Se solicitó envío forzado (forceSend)");
  } else {
    ESP_LOGW(TAG, "RPC desconocido: %s", method);
  }
}

// ==================== LIGHT SLEEP ====================
static void enter_light_sleep_ms(uint32_t ms, adc_continuous_handle_t adc_handle) {
  ESP_LOGI(TAG, "Preparando para entrar en light sleep %d ms...", ms);

  if (adc_handle) {
    adc_continuous_stop(adc_handle);
    ESP_LOGI(TAG, "ADC detenido antes de dormir");
  }

  mqtt_manager_disconnect();
  ESP_LOGI(TAG, "MQTT desconectado");

  esp_wifi_stop();
  vTaskDelay(pdMS_TO_TICKS(100));
  ESP_LOGI(TAG, "WiFi detenido, entrando en light sleep...");

  esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
  esp_light_sleep_start();

  ESP_LOGI(TAG, "Despertando de light sleep");

  esp_wifi_start();
  ESP_LOGI(TAG, "Esperando reconexión WiFi...");
  while (!wifi_manager_is_connected()) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  ESP_LOGI(TAG, "WiFi reconectado tras light sleep");
  vTaskDelay(pdMS_TO_TICKS(3000));
  if (adc_handle) {
    adc_continuous_start(adc_handle);
    ESP_LOGI(TAG, "ADC reiniciado tras light sleep");
  }

  mqtt_manager_reconnect();
  ESP_LOGI(TAG, "MQTT reconectado tras light sleep");
}

// ==================== MAIN ====================
void app_main(void) {
  // ---------- Inicialización WiFi ----------
  wifi_manager_init();
  ESP_LOGI(TAG, "Esperando conexión WiFi...");
  while (!wifi_manager_is_connected()) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  ESP_LOGI(TAG, "WiFi conectado.");

  // ---------- Inicialización Telegram ----------
  telegram_bot_start();
  ESP_LOGI(TAG, "Bot de Telegram iniciado correctamente");

  vTaskDelay(pdMS_TO_TICKS(2000)); // tiempo para estabilidad post arranque

  // ---------- Inicialización MQTT ----------
  mqtt_manager_init(MQTT_BROKER, MQTT_TOKEN);
  mqtt_manager_set_rpc_callback(handle_rpc);

  // ---------- Inicialización ADC/LDR ----------
  adc_continuous_handle_t adc_handle;
  if (adc_driver_init(&adc_handle) != ESP_OK) {
    ESP_LOGE(TAG, "Error inicializando ADC");
    return;
  }
  ldr_init(adc_handle);

  // ---------- Inicialización BME680 ----------
  if (bme680_init_sensor() != ESP_OK) {
    ESP_LOGE(TAG, "Error inicializando BME680");
  } else {
    ESP_LOGI(TAG, "BME680 inicializado correctamente");
  }

  // ---------- Bucle principal ----------
  while (true) {
    // LDR
    float resistance = ldr_get_resistance(adc_handle);
    uint8_t light_level = 0;
    if (resistance >= 0) {
      light_level = calculate_light_level(resistance);
      mqtt_manager_publish_light(light_level);
      ESP_LOGI(TAG, "LDR: %.2f Ω, Luz: %d%%", resistance, light_level);
    }

    // BME680
    bme680_data_t bme;
    if (bme680_read_data(&bme) == ESP_OK) {
      ESP_LOGI(TAG, "BME680 -> T: %.2f°C, H: %.2f%%, P: %.2f hPa, G: %.2f kΩ",
               bme.temperature, bme.humidity, bme.pressure, bme.gas_resistance);

      // Enviar datos a ThingsBoard
      char payload[256];
      snprintf(payload, sizeof(payload),
               "{\"temperature\":%.2f,\"humidity\":%.2f,\"pressure\":%.2f,"
               "\"gas\":%.2f,\"light\":%d}",
               bme.temperature, bme.humidity, bme.pressure,
               bme.gas_resistance, light_level);

      mqtt_manager_publish_json(payload);
      ESP_LOGI(TAG, "Datos enviados a ThingsBoard: %s", payload);
    } else {
      ESP_LOGW(TAG, "Error leyendo datos del BME680");
    }

    // Ahorro energético con reconexión segura
    enter_light_sleep_ms((uint32_t)g_sensor_interval_ms, adc_handle);
  }
}

