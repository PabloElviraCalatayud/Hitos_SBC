#include "mqtt_manager.h"
#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "MQTT_MANAGER";
static esp_mqtt_client_handle_t client = NULL;

void mqtt_manager_init(const char *broker_url, const char *token) {
  esp_mqtt_client_config_t mqtt_cfg = {
    .broker.address.uri = broker_url,
    .credentials.username = token
  };

  client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_start(client);
  ESP_LOGI(TAG, "Conectado a broker: %s", broker_url);
}

void mqtt_manager_publish_light(uint8_t light_level) {
  if (!client) {
    ESP_LOGW(TAG, "Cliente MQTT no inicializado");
    return;
  }

  char payload[64];
  snprintf(payload, sizeof(payload), "{\"light\": %d}", light_level);
  esp_mqtt_client_publish(client, "v1/devices/me/telemetry", payload, 0, 1, 0);
  ESP_LOGI(TAG, "Publicado nivel de luz: %d%%", light_level);
}

