#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include "TelegramCertificate.h"
#include <string.h>
#include <stdlib.h>
#include "cJSON.h"

static const char *TAG = "TELEGRAM_BOT";

/* --- CONFIGURACIÓN --- */
#define BOT_TOKEN "8246280825:AAFmw2GAGnrHzqIYUiq085FuAlPn7TuEqU0"
#define TELEGRAM_API_URL "https://api.telegram.org"
#define POLL_INTERVAL_MS 3000

/* --- VARIABLES GLOBALES --- */
static int delay_ms = 1000;
static long last_update_id = 0;

/* --- FUNCIONES PÚBLICAS --- */
int telegram_get_delay(void) {
  return delay_ms;
}

/* --- FUNCIONES INTERNAS --- */
static void telegram_send_message(const char *chat_id, const char *text) {
  if (!chat_id || !text) return;

  char url[256];
  snprintf(url, sizeof(url), TELEGRAM_API_URL "/bot%s/sendMessage", BOT_TOKEN);

  char safe_text[1024];
  int j = 0;
  for (int i = 0; text[i] != '\0' && j < sizeof(safe_text) - 2; i++) {
    if (text[i] == '\n') {
      safe_text[j++] = '\\';
      safe_text[j++] = 'n';
    } else if (text[i] == '\"') {
      safe_text[j++] = '\\';
      safe_text[j++] = '\"';
    } else {
      safe_text[j++] = text[i];
    }
  }
  safe_text[j] = '\0';

  char post_data[1152];
  snprintf(post_data, sizeof(post_data),
           "{\"chat_id\":%s,\"text\":\"%s\",\"parse_mode\":\"Markdown\"}",
           chat_id, safe_text);

  esp_http_client_config_t config = {
    .url = url,
    .method = HTTP_METHOD_POST,
    .cert_pem = TELEGRAM_CERTIFICATE_ROOT,
    .timeout_ms = 8000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGE(TAG, "Fallo al crear cliente HTTP");
    return;
  }

  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, post_data, strlen(post_data));

  esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Mensaje enviado (HTTP %d): %s", status, text);
  } else {
    ESP_LOGE(TAG, "Error enviando mensaje: %s", esp_err_to_name(err));
  }

  esp_http_client_cleanup(client);
}

static void handle_command(const char *chat_id, const char *text) {
  if (!chat_id || !text) return;

  if (strncmp(text, "/status", 7) == 0) {
    char msg[256];
    snprintf(msg, sizeof(msg),
             "📡 *Estado actual del ESP32:*\n"
             "✅ Dispositivo activo\n"
             "⏱ Retardo actual: %d ms\n"
             "💾 Memoria libre: %d bytes",
             delay_ms, (int)esp_get_free_heap_size());
    telegram_send_message(chat_id, msg);
  } else if (strncmp(text, "/speed", 6) == 0) {
    const char *arg = text + 6;
    while (*arg == ' ') arg++;
    int newDelay = atoi(arg);
    if (newDelay > 0 && newDelay <= 10000) {
      delay_ms = newDelay;
      char msg[128];
      snprintf(msg, sizeof(msg), "⚙️ Retardo actualizado a %d ms", delay_ms);
      telegram_send_message(chat_id, msg);
    } else {
      telegram_send_message(chat_id, "❌ Uso correcto: /speed <1-10000>");
    }
  } else if (strncmp(text, "/help", 5) == 0) {
    telegram_send_message(chat_id,
      "📘 *Comandos disponibles:*\n"
      "/status - Estado del ESP32\n"
      "/speed <ms> - Cambiar retardo de lectura\n"
      "/help - Mostrar esta ayuda");
  } else {
    telegram_send_message(chat_id,
      "❓ Comando no reconocido.\nUsa /help para ver la lista.");
  }
}

/* --- EVENTOS HTTP --- */
esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
  switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
      if (evt->user_data) {
        char *user_buffer = (char *)evt->user_data;
        int current_len = strlen(user_buffer);
        if (current_len + evt->data_len < 8192) {
          memcpy(user_buffer + current_len, evt->data, evt->data_len);
          user_buffer[current_len + evt->data_len] = '\0';
        }
      }
      break;
    default:
      break;
  }
  return ESP_OK;
}

/* --- ELIMINAR WEBHOOK --- */
static void telegram_delete_webhook(void) {
  char url[256];
  snprintf(url, sizeof(url), TELEGRAM_API_URL "/bot%s/deleteWebhook", BOT_TOKEN);

  esp_http_client_config_t config = {
    .url = url,
    .method = HTTP_METHOD_GET,
    .cert_pem = TELEGRAM_CERTIFICATE_ROOT,
    .timeout_ms = 5000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_perform(client);
  esp_http_client_cleanup(client);

  ESP_LOGI(TAG, "Webhook eliminado (si existía).");
}

/* --- OBTENER ACTUALIZACIONES --- */
void telegram_get_updates(void) {
  size_t free_heap_before = esp_get_free_heap_size();
  ESP_LOGI(TAG, "Free heap antes de HTTP: %d bytes", free_heap_before);

  char url[512];
  snprintf(url, sizeof(url),
           TELEGRAM_API_URL "/bot%s/getUpdates?offset=%ld&timeout=2&limit=1",
           BOT_TOKEN, last_update_id + 1);

  char local_response_buffer[8192] = {0};

  esp_http_client_config_t config = {
    .url = url,
    .method = HTTP_METHOD_GET,
    .cert_pem = TELEGRAM_CERTIFICATE_ROOT,
    .timeout_ms = 8000,
    .event_handler = _http_event_handler,
    .user_data = local_response_buffer,
    .buffer_size = 4096,
    .buffer_size_tx = 1024,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGE(TAG, "No se pudo crear el cliente HTTP");
    return;
  }

  esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    cJSON *root = cJSON_Parse(local_response_buffer);
    if (!root) {
      ESP_LOGE(TAG, "Error parseando JSON");
      esp_http_client_cleanup(client);
      return;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (cJSON_IsArray(result)) {
      int n = cJSON_GetArraySize(result);
      for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(result, i);
        cJSON *update_id = cJSON_GetObjectItem(item, "update_id");
        if (update_id) last_update_id = update_id->valueint;

        cJSON *message = cJSON_GetObjectItem(item, "message");
        if (message) {
          cJSON *chat = cJSON_GetObjectItem(message, "chat");
          cJSON *chat_id = chat ? cJSON_GetObjectItem(chat, "id") : NULL;
          cJSON *text = cJSON_GetObjectItem(message, "text");

          if (chat_id && text) {
            long long chat_id_ll = (long long)chat_id->valuedouble;
            char id_str[32];
            snprintf(id_str, sizeof(id_str), "%lld", chat_id_ll);
            ESP_LOGI(TAG, "📩 Mensaje recibido: %s", text->valuestring);
            handle_command(id_str, text->valuestring);
          }
        }
      }
    }
    cJSON_Delete(root);
  } else {
    ESP_LOGE(TAG, "HTTP GET falló: %s", esp_err_to_name(err));
  }

  esp_http_client_cleanup(client);
  size_t free_heap_after = esp_get_free_heap_size();
  ESP_LOGI(TAG, "Free heap después de HTTP: %d bytes (diff: %d)",
           free_heap_after, (int)(free_heap_after - free_heap_before));
}

/* --- TAREA PRINCIPAL DEL BOT --- */
static void telegram_bot_task(void *pvParameters) {
  ESP_LOGI(TAG, "Bot de Telegram iniciado.");
  telegram_delete_webhook();
  while (true) {
    telegram_get_updates();
    vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
  }
}

/* --- INICIO DEL BOT --- */
void telegram_bot_start(void) {
  ESP_LOGI(TAG, "Lanzando tarea Telegram...");
  xTaskCreatePinnedToCore(
    telegram_bot_task,
    "telegram_bot_task",
    16384,
    NULL,
    4,
    NULL,
    1
  );
}

