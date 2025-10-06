// wifi_manager.c  (versión revisada)
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

/* Event group y bit de conexión */
static EventGroupHandle_t s_wifi_event_group;
const int CONNECTED_BIT = BIT0;

#define WIFI_CRED_FILE "/spiffs/wifi.txt"
#define MAX_RETRIES    5
#define RETRY_DELAY_MS 200

static int retry_count = 0;
static bool wifi_connected = false;

// Convierte "%xx" en el caracter correspondiente
static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a = a - 'A' + 10;
            else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b = b - 'A' + 10;
            else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';  // "+" se interpreta como espacio
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}


/* ----------------- Util: guardar / cargar credenciales ----------------- */
bool save_wifi_credentials(const char *ssid, const char *pass)
{
    FILE *f = fopen(WIFI_CRED_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "No se pudo abrir %s para escritura", WIFI_CRED_FILE);
        return false;
    }
    fprintf(f, "%s\n%s\n", ssid ? ssid : "", pass ? pass : "");
    fflush(f);
    fclose(f);
    ESP_LOGI(TAG, "Credenciales guardadas en %s", WIFI_CRED_FILE);
    return true;
}

bool load_wifi_credentials(char *ssid, char *pass)
{
    FILE *f = fopen(WIFI_CRED_FILE, "r");
    if (!f) {
        ESP_LOGW(TAG, "No hay credenciales guardadas (%s)", WIFI_CRED_FILE);
        return false;
    }
    if (fgets(ssid, MAX_SSID_LEN, f) == NULL || fgets(pass, MAX_PASS_LEN, f) == NULL) {
        ESP_LOGE(TAG, "Error leyendo credenciales");
        fclose(f);
        return false;
    }
    /* quitar \r\n */
    ssid[strcspn(ssid, "\r\n")] = 0;
    pass[strcspn(pass, "\r\n")] = 0;
    fclose(f);
    ESP_LOGI(TAG, "Credenciales cargadas: SSID='%s'", ssid);
    return true;
}

/* ----------------- Evento WiFi ----------------- */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA arrancada -> intentando conectar");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        if (retry_count < MAX_RETRIES) {
            ESP_LOGW(TAG, "Fallo de conexión, reintentando (%d/%d)", retry_count + 1, MAX_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
            esp_wifi_connect();
            retry_count++;
        } else {
            ESP_LOGE(TAG, "No se pudo conectar tras %d intentos", MAX_RETRIES);
        }
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *) event_data;
        retry_count = 0;
        wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);

        /* intentar obtener SSID al que estamos conectados */
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "Conectado con IP " IPSTR " SSID: %s",
                     IP2STR(&ev->ip_info.ip), (char*)ap_info.ssid);
        } else {
            ESP_LOGI(TAG, "Conectado con IP " IPSTR " (no se pudo obtener SSID)",
                     IP2STR(&ev->ip_info.ip));
        }
    }
}

/* ----------------- Handlers HTTP ----------------- */

/* GET / -> redirige a /scan */
static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/scan");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* GET /scan -> escanea redes y devuelve HTML dinámico en chunks (seguro) */
static esp_err_t scan_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Iniciando escaneo WiFi...");

    esp_wifi_disconnect();
    wifi_scan_config_t scanConf = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 100, .max = 300 } }
    };

    if (esp_wifi_scan_start(&scanConf, true) != ESP_OK) {
        httpd_resp_sendstr(req, "Scan failed");
        return ESP_FAIL;
    }

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);

    wifi_ap_record_t *ap_records = calloc(ap_num, sizeof(wifi_ap_record_t));
    esp_wifi_scan_get_ap_records(&ap_num, ap_records);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>ESP32 WiFi</title>"
        "<style>"
        "body{margin:0;font-family:Inter,Roboto,Arial;background:#000;color:#fff;text-align:center;}"
        "h1{font-weight:400;margin:20px 0;}"
        ".netlist{display:flex;flex-direction:column;align-items:center;gap:12px;padding:10px;}"
        ".card{width:90%;max-width:400px;background:#111;border:1px solid #222;border-radius:16px;padding:16px;box-shadow:0 0 8px rgba(255,255,255,0.05);text-align:left;}"
        ".ssid{font-size:18px;font-weight:500;}"
        ".rssi{font-size:14px;color:#aaa;margin-bottom:12px;}"
        "input[type=password]{width:100%;padding:8px;border:none;border-radius:8px;margin-bottom:12px;}"
        ".btn{display:block;width:100%;padding:12px;border:none;border-radius:24px;background:#fff;color:#000;font-weight:600;font-size:16px;cursor:pointer;transition:0.2s;}"
        ".btn:hover{background:#ddd;}"
        "</style></head><body><h1>Redes WiFi</h1><div class='netlist'>");

    for (int i = 0; i < ap_num; i++) {
        char ssid[33];
        memcpy(ssid, ap_records[i].ssid, sizeof(ap_records[i].ssid));
        ssid[32] = '\0';

        char chunk[512];
        snprintf(chunk, sizeof(chunk),
            "<div class='card'>"
            "<div class='ssid'>%s</div>"
            "<div class='rssi'>Señal: %d dBm</div>"
            "<form method='POST' action='/config'>"
            "<input type='hidden' name='ssid' value='%s'>"
            "<input type='password' name='pass' placeholder='Contraseña'>"
            "<button class='btn' type='submit'>Conectar</button>"
            "</form></div>",
            ssid, ap_records[i].rssi, ssid);

        httpd_resp_sendstr_chunk(req, chunk);
    }

    httpd_resp_sendstr_chunk(req, "</div></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);

    free(ap_records);
    return ESP_OK;
}
/* POST /config -> recibe form-urlencoded (ssid, pass) y aplica config STA */
static esp_err_t config_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = 0;

    // Buffers crudos
    char raw_ssid[64] = {0};
    char raw_pass[64] = {0};

    // Extraer (aún codificado)
    sscanf(buf, "ssid=%63[^&]&pass=%63s", raw_ssid, raw_pass);

    // Buffers finales decodificados
    char ssid[64], pass[64];
    url_decode(ssid, raw_ssid);
    url_decode(pass, raw_pass);

    ESP_LOGI(TAG, "Guardando SSID: [%s] PASS: [%s]", ssid, pass);

    FILE *f = fopen(WIFI_CRED_FILE, "w");
    if (f) {
        fprintf(f, "%s\n%s\n", ssid, pass);
        fclose(f);
    }

    httpd_resp_sendstr(req, "Guardado, reiniciando...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

/* ----------------- Start webserver: registra rutas ----------------- */
httpd_handle_t wifi_manager_start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return NULL;
    }

    httpd_uri_t index_uri = {
        .uri = "/", .method = HTTP_GET, .handler = index_get_handler, .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &index_uri);

    httpd_uri_t scan_uri = {
        .uri = "/scan", .method = HTTP_GET, .handler = scan_get_handler, .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scan_uri);

    httpd_uri_t conf_uri = {
        .uri = "/config", .method = HTTP_POST, .handler = config_post_handler, .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &conf_uri);

    ESP_LOGI(TAG, "Webserver iniciado");
    return server;
}

/* ----------------- start AP+STA ----------------- */
static void start_wifi_apsta(const char *ssid, const char *pass)
{
    /* Crear interfaces por defecto */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Registrar manejadores de evento */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    /* Poner modo APSTA y arrancar el driver */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Configurar STA si hay credenciales guardadas */
    if (ssid && pass && strlen(ssid) > 0) {
        wifi_config_t sta_config = {0};
        strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
        strncpy((char *)sta_config.sta.password, pass, sizeof(sta_config.sta.password) - 1);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    }

    /* Construir SSID AP con últimos 3 bytes MAC */
    uint8_t mac[6];
    if (esp_wifi_get_mac(WIFI_IF_AP, mac) != ESP_OK) {
        esp_wifi_get_mac(WIFI_IF_STA, mac);  // fallback
    }
    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "ESP32_2025_%02X%02X%02X", mac[3], mac[4], mac[5]);

    wifi_config_t ap_config = {0};
    strncpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.channel = 0;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    ESP_LOGI(TAG, "Modo APSTA iniciado. AP SSID: %s", ap_ssid);

    /* Arrancar servidor web */
    wifi_manager_start_webserver();
}

/* ----------------- API pública ----------------- */
void wifi_manager_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Montar SPIFFS */
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&spiffs_conf));

    s_wifi_event_group = xEventGroupCreate();

    char ssid[MAX_SSID_LEN] = {0}, pass[MAX_PASS_LEN] = {0};
    if (load_wifi_credentials(ssid, pass)) {
        start_wifi_apsta(ssid, pass);
    } else {
        start_wifi_apsta(NULL, NULL);
    }
}

bool wifi_manager_is_connected(void)
{
    return wifi_connected;
}


