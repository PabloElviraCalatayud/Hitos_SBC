#include "ota_manager.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static const char *TAG = "ota_manager";

#define OTA_NAMESPACE "ota_state"
#define OTA_FLAG_KEY  "pending"

static void set_ota_pending_flag(bool value) {
  nvs_handle_t nvs;
  if (nvs_open(OTA_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
    nvs_set_u8(nvs, OTA_FLAG_KEY, value ? 1 : 0);
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Flag OTA pendiente = %d", value);
  } else {
    ESP_LOGE(TAG, "Error accediendo a NVS para flag OTA");
  }
}

bool ota_has_pending_update(void) {
  nvs_handle_t nvs;
  uint8_t flag = 0;
  if (nvs_open(OTA_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
    nvs_get_u8(nvs, OTA_FLAG_KEY, &flag);
    nvs_close(nvs);
  }
  return flag == 1;
}

void ota_apply_pending_update(void) {
  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  ESP_LOGI(TAG, "Aplicando OTA pendiente desde partición: %s", update_partition->label);
  if (esp_ota_set_boot_partition(update_partition) == ESP_OK) {
    ESP_LOGI(TAG, "Partición de arranque cambiada. Aplicando actualización en siguiente reinicio...");
    set_ota_pending_flag(false);
  } else {
    ESP_LOGE(TAG, "Error configurando partición OTA");
  }
}

static esp_err_t ota_post_handler(httpd_req_t *req) {
  esp_ota_handle_t ota_handle;
  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  ESP_LOGI(TAG, "Iniciando OTA en partición: %s", update_partition->label);

  if (esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Fallo esp_ota_begin");
    return ESP_FAIL;
  }

  char buf[1024];
  int total = 0;
  int remaining = req->content_len;
  bool first_chunk_checked = false;

  while (remaining > 0) {
    int to_read = MIN(remaining, sizeof(buf));
    int received = httpd_req_recv(req, buf, to_read);
    if (received <= 0) {
      ESP_LOGE(TAG, "Error recibiendo firmware");
      esp_ota_end(ota_handle);
      return ESP_FAIL;
    }

    if (!first_chunk_checked) {
      if ((uint8_t)buf[0] != 0xE9) {
        ESP_LOGE(TAG, "Firmware inválido (0x%02X)", (uint8_t)buf[0]);
        esp_ota_end(ota_handle);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Archivo OTA inválido");
        return ESP_FAIL;
      }
      first_chunk_checked = true;
    }

    if (esp_ota_write(ota_handle, buf, received) != ESP_OK) {
      esp_ota_end(ota_handle);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error escribiendo flash");
      return ESP_FAIL;
    }

    total += received;
    remaining -= received;
  }

  if (esp_ota_end(ota_handle) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Fallo esp_ota_end");
    return ESP_FAIL;
  }

  // ✅ No cambiamos partición ni reiniciamos aquí
  // Solo marcamos que hay una actualización pendiente
  set_ota_pending_flag(true);

  ESP_LOGI(TAG, "Firmware almacenado (%d bytes). Pendiente de reinicio manual.", total);
  httpd_resp_sendstr(req, "Firmware cargado correctamente. Aplicarás la actualización en el próximo reinicio manual.");
  return ESP_OK;
}

// ------------------ Página HTML OTA ------------------
static esp_err_t ota_get_handler(httpd_req_t *req) {
  const char *html =
    "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<style>"
    "body{font-family:Arial, sans-serif; background:#f7f9fb; display:flex; justify-content:center; align-items:center; height:100vh; margin:0;}"
    ".card{background:white; padding:30px; border-radius:12px; box-shadow:0 4px 10px rgba(0,0,0,0.1); text-align:center; width:90%; max-width:400px;}"
    "h2{color:#333; margin-bottom:20px;}"
    "input[type=file]{margin-bottom:20px;}"
    "button{background:#0078D7; color:white; border:none; padding:10px 20px; border-radius:6px; cursor:pointer; transition:background 0.2s;}"
    "button:hover{background:#005fa3;}"
    "#log{margin-top:20px; color:#444; font-size:0.9em; white-space:pre-line; text-align:left;}"
    "</style></head><body>"
    "<div class='card'>"
    "<h2>Actualización OTA</h2>"
    "<input type='file' id='firmware' accept='.bin'><br>"
    "<button onclick='upload()'>Subir firmware</button>"
    "<pre id='log'></pre>"
    "<hr><button onclick='reboot()'>Reiniciar dispositivo</button>"
    "</div>"
    "<script>"
    "async function upload(){"
      "const file=document.getElementById('firmware').files[0];"
      "const log=document.getElementById('log');"
      "if(!file){alert('Selecciona un archivo .bin');return;}"
      "log.textContent='Subiendo '+file.name+' ('+file.size+' bytes)...';"
      "const res=await fetch('/update',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:file});"
      "const txt=await res.text();"
      "log.textContent+='\\n'+txt;"
    "}"
    "async function reboot(){"
      "const res=await fetch('/reboot',{method:'POST'});"
      "if(res.ok){alert('Reiniciando...');}"
      "else{alert('Error reiniciando');}"
    "}"
    "</script></body></html>";

  httpd_resp_set_type(req, "text/html");
  httpd_resp_sendstr(req, html);
  return ESP_OK;
}

// ------------------ Endpoint /reboot ------------------
static esp_err_t reboot_post_handler(httpd_req_t *req) {
  httpd_resp_sendstr(req, "Reiniciando...");
  ESP_LOGI(TAG, "Reinicio manual solicitado vía /reboot");
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
  return ESP_OK;
}

// ------------------ Registro de endpoints ------------------
esp_err_t ota_register_endpoints(httpd_handle_t server) {
  httpd_uri_t update_get = {
    .uri = "/update",
    .method = HTTP_GET,
    .handler = ota_get_handler
  };
  httpd_uri_t update_post = {
    .uri = "/update",
    .method = HTTP_POST,
    .handler = ota_post_handler
  };
  httpd_uri_t reboot_post = {
    .uri = "/reboot",
    .method = HTTP_POST,
    .handler = reboot_post_handler
  };

  esp_err_t res;
  if ((res = httpd_register_uri_handler(server, &update_get)) != ESP_OK) return res;
  if ((res = httpd_register_uri_handler(server, &update_post)) != ESP_OK) return res;
  return httpd_register_uri_handler(server, &reboot_post);
}


