#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"

#include "wifi_manager.h"

static const char *TAG = "wifi_manager";

#define WIFI_CRED_FILE "/spiffs/wifi.txt"
#define MAX_RETRIES    5
#define RETRY_DELAY_MS 200

static EventGroupHandle_t s_wifi_event_group;
const int CONNECTED_BIT = BIT0;

static int retry_count = 0;
static bool wifi_connected = false;

/* ---------------- URL Decode ---------------- */
static void url_decode(char *dst, const char *src) {
  char a, b;
  while (*src) {
    if ((*src == '%') &&
        ((a = src[1]) && (b = src[2])) &&
        (isxdigit(a) && isxdigit(b))) {
      if (a >= 'a') a -= 'a' - 'A';
      a = (a >= 'A') ? a - 'A' + 10 : a - '0';
      if (b >= 'a') b -= 'a' - 'A';
      b = (b >= 'A') ? b - 'A' + 10 : b - '0';
      *dst++ = 16 * a + b;
      src += 3;
    } else if (*src == '+') {
      *dst++ = ' ';
      src++;
    } else {
      *dst++ = *src++;
    }
  }
  *dst = '\0';
}

/* ---------------- Guardar / Cargar credenciales ---------------- */
bool save_wifi_credentials(const char *ssid, const char *pass) {
  FILE *f = fopen(WIFI_CRED_FILE, "w");
  if (!f) {
    ESP_LOGE(TAG, "No se pudo abrir %s para escritura", WIFI_CRED_FILE);
    return false;
  }
  fprintf(f, "%s\n%s\n", ssid ? ssid : "", pass ? pass : "");
  fclose(f);
  ESP_LOGI(TAG, "Credenciales guardadas: SSID='%s'", ssid);
  return true;
}

bool load_wifi_credentials(char *ssid, char *pass) {
  FILE *f = fopen(WIFI_CRED_FILE, "r");
  if (!f) {
    ESP_LOGW(TAG, "No hay credenciales guardadas");
    return false;
  }
  if (!fgets(ssid, WIFI_MANAGER_MAX_SSID_LEN, f) || !fgets(pass, WIFI_MANAGER_MAX_PASS_LEN, f)) {
    fclose(f);
    ESP_LOGE(TAG, "Error leyendo credenciales");
    return false;
  }
  ssid[strcspn(ssid, "\r\n")] = 0;
  pass[strcspn(pass, "\r\n")] = 0;
  fclose(f);
  ESP_LOGI(TAG, "Credenciales cargadas: SSID='%s'", ssid);
  return true;
}

/* ---------------- Manejador de eventos ---------------- */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_connected = false;
    if (retry_count < MAX_RETRIES) {
      retry_count++;
      vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
      esp_wifi_connect();
      ESP_LOGW(TAG, "Reintentando conexión (%d/%d)", retry_count, MAX_RETRIES);
    } else {
      ESP_LOGE(TAG, "No se pudo conectar al WiFi");
    }
    xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Conectado. IP local: " IPSTR, IP2STR(&event->ip_info.ip));
    retry_count = 0;
    wifi_connected = true;
    xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
  }
}

/* ---------------- Servidor Web ---------------- */
static esp_err_t index_get_handler(httpd_req_t *req) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/scan");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t scan_get_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "Escaneando redes...");
  wifi_scan_config_t scan_conf = {
    .ssid = NULL,
    .bssid = NULL,
    .channel = 0,
    .show_hidden = true
  };
  esp_wifi_scan_start(&scan_conf, true);

  uint16_t ap_num = 0;
  esp_wifi_scan_get_ap_num(&ap_num);
  wifi_ap_record_t *ap_records = calloc(ap_num, sizeof(wifi_ap_record_t));
  esp_wifi_scan_get_ap_records(&ap_num, ap_records);

  httpd_resp_set_type(req, "text/html");
  httpd_resp_sendstr_chunk(req, "<html><body><h2>Redes WiFi disponibles</h2>");
  for (int i = 0; i < ap_num; i++) {
    char ssid[33];
    memcpy(ssid, ap_records[i].ssid, sizeof(ap_records[i].ssid));
    ssid[32] = 0;
    char line[512];
    snprintf(line, sizeof(line),
      "<form method='POST' action='/config'>"
      "<b>%s</b> (%d dBm)<br>"
      "<input type='hidden' name='ssid' value='%s'>"
      "Contraseña: <input type='password' name='pass'><br>"
      "<input type='submit' value='Conectar'></form><hr>",
      ssid, ap_records[i].rssi, ssid);
    httpd_resp_sendstr_chunk(req, line);
  }
  httpd_resp_sendstr_chunk(req, "</body></html>");
  httpd_resp_sendstr_chunk(req, NULL);

  free(ap_records);
  return ESP_OK;
}

static esp_err_t config_post_handler(httpd_req_t *req) {
  char buf[256];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) return ESP_FAIL;
  buf[ret] = '\0';

  char raw_ssid[WIFI_MANAGER_MAX_SSID_LEN] = {0};
  char raw_pass[WIFI_MANAGER_MAX_PASS_LEN] = {0};
  sscanf(buf, "ssid=%63[^&]&pass=%63s", raw_ssid, raw_pass);

  char ssid[WIFI_MANAGER_MAX_SSID_LEN];
  char pass[WIFI_MANAGER_MAX_PASS_LEN];
  url_decode(ssid, raw_ssid);
  url_decode(pass, raw_pass);

  ESP_LOGI(TAG, "Guardando SSID='%s', PASS='%s'", ssid, pass);
  save_wifi_credentials(ssid, pass);

  httpd_resp_sendstr(req, "Credenciales guardadas. Reiniciando...");
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
  return ESP_OK;
}

static httpd_handle_t start_webserver(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler };
    httpd_uri_t scan_uri = { .uri = "/scan", .method = HTTP_GET, .handler = scan_get_handler };
    httpd_uri_t config_uri = { .uri = "/config", .method = HTTP_POST, .handler = config_post_handler };
    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &scan_uri);
    httpd_register_uri_handler(server, &config_uri);
    ESP_LOGI(TAG, "Servidor web iniciado");
  }
  return server;
}

/* ---------------- AP + STA ---------------- */
static void start_wifi_apsta(const char *ssid, const char *pass) {
  esp_netif_create_default_wifi_ap();
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

  // Configurar AP
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_AP, mac);
  char ap_ssid[32];
  snprintf(ap_ssid, sizeof(ap_ssid), "ESP32_%02X%02X%02X", mac[3], mac[4], mac[5]);

  wifi_config_t ap_config = {0};
  strncpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid) - 1);
  ap_config.ap.max_connection = 4;
  ap_config.ap.authmode = WIFI_AUTH_OPEN;
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

  // Configurar STA si hay credenciales
  if (ssid && pass && strlen(ssid) > 0) {
    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, pass, sizeof(sta_config.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
  }

  ESP_ERROR_CHECK(esp_wifi_start());
  start_webserver();

  ESP_LOGI(TAG, "AP iniciado con SSID: %s", ap_ssid);
}

/* ---------------- API pública ---------------- */
void wifi_manager_init(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_vfs_spiffs_conf_t spiffs_conf = {
    .base_path = "/spiffs",
    .partition_label = NULL,
    .max_files = 5,
    .format_if_mount_failed = true
  };
  ESP_ERROR_CHECK(esp_vfs_spiffs_register(&spiffs_conf));

  s_wifi_event_group = xEventGroupCreate();

  char ssid[WIFI_MANAGER_MAX_SSID_LEN] = {0};
  char pass[WIFI_MANAGER_MAX_PASS_LEN] = {0};
  if (load_wifi_credentials(ssid, pass)) {
    start_wifi_apsta(ssid, pass);
  } else {
    start_wifi_apsta(NULL, NULL);
  }
}

bool wifi_manager_is_connected(void) {
  return wifi_connected;
}

