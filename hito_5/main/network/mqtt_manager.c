#include "mqtt_manager.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "MQTT_MANAGER";
static esp_mqtt_client_handle_t client = NULL;
static mqtt_rpc_cb_t s_rpc_cb = NULL;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = event_data;
  switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
      ESP_LOGI(TAG, "MQTT conectado");
      // Suscribir a RPCs desde ThingsBoard
      esp_mqtt_client_subscribe(client, "v1/devices/me/rpc/request/+", 1);
      break;

    case MQTT_EVENT_DATA:
      ESP_LOGI(TAG, "MQTT_EVENT_DATA topic=%.*s", event->topic_len, event->topic);
      // Copiar topic y payload a C-strings
      {
        char topic[128] = {0};
        char data[512] = {0};
        int tlen = event->topic_len < (int)sizeof(topic)-1 ? event->topic_len : (int)sizeof(topic)-1;
        int dlen = event->data_len < (int)sizeof(data)-1 ? event->data_len : (int)sizeof(data)-1;
        memcpy(topic, event->topic, tlen);
        memcpy(data, event->data, dlen);
        topic[tlen] = '\0';
        data[dlen] = '\0';

        // RPC topic pattern: v1/devices/me/rpc/request/<id>
        if (strncmp(topic, "v1/devices/me/rpc/request/", 26) == 0) {
          // payload ejemplo: {"method":"setInterval","params":{"interval":5000}}
          if (s_rpc_cb) {
            // parse method and params quickly (lightweight)
            cJSON *root = cJSON_Parse(data);
            if (root) {
              cJSON *method = cJSON_GetObjectItem(root, "method");
              cJSON *params = cJSON_GetObjectItem(root, "params");
              const char *method_str = method && cJSON_IsString(method) ? method->valuestring : NULL;
              char *params_str = NULL;
              if (params) {
                params_str = cJSON_PrintUnformatted(params);
              }
              if (method_str) {
                s_rpc_cb(method_str, params_str ? params_str : "{}");
              }
              if (params_str) free(params_str);
              cJSON_Delete(root);
            } else {
              ESP_LOGW(TAG, "RPC payload no JSON");
              s_rpc_cb("unknown", data);
            }
          } else {
            ESP_LOGW(TAG, "RPC recibido pero no hay callback registrado");
          }
        }
      }
      break;

    default:
      break;
  }
}

void mqtt_manager_init(const char *broker_url, const char *token) {
  if (client) return;
  esp_mqtt_client_config_t mqtt_cfg = {
    .broker.address.uri = broker_url,
    .credentials.username = token
  };

  client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
  esp_mqtt_client_start(client);
  ESP_LOGI(TAG, "Iniciando MQTT con broker: %s", broker_url);
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

void mqtt_manager_publish_env(float temperature, float humidity) {
  if (!client) {
    ESP_LOGW(TAG, "Cliente MQTT no inicializado");
    return;
  }
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "temperature", temperature);
  cJSON_AddNumberToObject(root, "humidity", humidity);
  char *payload = cJSON_PrintUnformatted(root);
  esp_mqtt_client_publish(client, "v1/devices/me/telemetry", payload, 0, 1, 0);
  ESP_LOGI(TAG, "Publicado temp: %.1f C, hum: %.1f %%", temperature, humidity);
  free(payload);
  cJSON_Delete(root);
}

void mqtt_manager_set_rpc_callback(mqtt_rpc_cb_t cb) {
  s_rpc_cb = cb;
}

void mqtt_manager_disconnect(void) {
  if (client) {
    esp_mqtt_client_stop(client);
    ESP_LOGI(TAG, "Cliente MQTT detenido correctamente");
  } else {
    ESP_LOGW(TAG, "mqtt_manager_disconnect llamado sin cliente válido");
  }
}

void mqtt_manager_reconnect(void) {
  if (client) {
    esp_mqtt_client_start(client);
    ESP_LOGI(TAG, "Cliente MQTT reiniciado correctamente");
  } else {
    ESP_LOGW(TAG, "mqtt_manager_reconnect llamado sin cliente válido");
  }
}

