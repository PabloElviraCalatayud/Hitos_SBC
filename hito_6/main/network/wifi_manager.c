#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "ota/ota_manager.h"
#include <ctype.h>

static const char *TAG = "wifi_manager";
static httpd_handle_t server = NULL;
static bool wifi_connected = false;

#define WIFI_CRED_FILE "/spiffs/wifi.txt"

#define WIFI_MANAGER_MAX_SSID_LEN 32
#define WIFI_MANAGER_MAX_PASS_LEN 64

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
static bool save_wifi_credentials(const char *ssid, const char *pass) {
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

static bool load_wifi_credentials(char *ssid, char *pass) {
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

/* ---------------- Manejador de eventos WiFi ---------------- */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_connected = false;
    ESP_LOGW(TAG, "Desconectado, reintentando...");
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    wifi_connected = true;
    ESP_LOGI(TAG, "Conectado a WiFi con IP");
  }
}

/* ---------------- Handlers HTTP ---------------- */
static esp_err_t root_redirect_handler(httpd_req_t *req) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/scan");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t scan_get_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "Escaneando redes WiFi...");
  wifi_scan_config_t scan_config = {
    .ssid = NULL,
    .bssid = NULL,
    .channel = 0,
    .show_hidden = true
  };
  esp_wifi_scan_start(&scan_config, true);

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
  sscanf(buf, "ssid=%31[^&]&pass=%63s", raw_ssid, raw_pass);

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

/* ---------------- Servidor web ---------------- */
static void start_webserver(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  // Ajustes para evitar timeouts durante subidas OTA grandes
  config.stack_size = 8192;               // más stack para la tarea del servidor
  config.recv_wait_timeout = 10;          // segundos de espera para recv
  config.send_wait_timeout = 10;          // segundos
  config.lru_purge_enable = true;
  config.max_open_sockets = 6;

  if (httpd_start(&server, &config) == ESP_OK) {
    ESP_LOGI(TAG, "Servidor HTTP iniciado en el puerto %d", config.server_port);

    httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_redirect_handler };
    httpd_uri_t scan_uri = { .uri = "/scan", .method = HTTP_GET, .handler = scan_get_handler };
    httpd_uri_t config_uri = { .uri = "/config", .method = HTTP_POST, .handler = config_post_handler };

    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &scan_uri);
    httpd_register_uri_handler(server, &config_uri);

    // 🔧 Registrar endpoints OTA
    ota_register_endpoints(server);
  } else {
    ESP_LOGE(TAG, "Error iniciando servidor HTTP");
  }
}

/* ---------------- Inicialización WiFi ---------------- */
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

  esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

  // Crear AP
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_AP, mac);
  char ap_ssid[32];
  snprintf(ap_ssid, sizeof(ap_ssid), "ESP32_%02X%02X%02X", mac[3], mac[4], mac[5]);

  wifi_config_t ap_config = {0};
  strncpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid) - 1);
  ap_config.ap.max_connection = 4;
  ap_config.ap.authmode = WIFI_AUTH_OPEN;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

  // Intentar conectar con credenciales guardadas
  char ssid[WIFI_MANAGER_MAX_SSID_LEN] = {0};
  char pass[WIFI_MANAGER_MAX_PASS_LEN] = {0};
  if (load_wifi_credentials(ssid, pass)) {
    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, pass, sizeof(sta_config.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
  }

  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
  start_webserver();

  ESP_LOGI(TAG, "Punto de acceso iniciado: %s", ap_ssid);
}

bool wifi_manager_is_connected(void) {
  return wifi_connected;
}

