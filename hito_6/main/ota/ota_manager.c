#include "ota_manager.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include <string.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static const char *TAG = "ota_manager";

static esp_err_t ota_post_handler(httpd_req_t *req) {
  esp_ota_handle_t ota_handle;
  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  ESP_LOGI(TAG, "Iniciando OTA en partición: %s", update_partition->label);

  if (esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle) != ESP_OK) {
    ESP_LOGE(TAG, "Error iniciando OTA");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Fallo en esp_ota_begin");
    return ESP_FAIL;
  }

  char buf[1024];
  int total = 0;
  int remaining = req->content_len;

  if (remaining <= 0) {
    ESP_LOGW(TAG, "Content-Length inválido o desconocido: %d", remaining);
    esp_ota_end(ota_handle);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content-Length inválido");
    return ESP_FAIL;
  }

  const int MAX_RECV_RETRIES = 8;
  int recv_retry = 0;
  bool first_chunk_checked = false;

  while (remaining > 0) {
    int to_read = MIN(remaining, sizeof(buf));
    int received = httpd_req_recv(req, buf, to_read);

    if (received <= 0) {
      recv_retry++;
      if (recv_retry > MAX_RECV_RETRIES) {
        ESP_LOGE(TAG, "Error recibiendo firmware (retries agotados)");
        esp_ota_end(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error recibiendo datos");
        return ESP_FAIL;
      }
      ESP_LOGW(TAG, "httpd_req_recv devolvió %d, reintentando (%d/%d)...", received, recv_retry, MAX_RECV_RETRIES);
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    recv_retry = 0;

    // ✅ Validar primer byte del primer bloque recibido
    if (!first_chunk_checked) {
      if ((uint8_t)buf[0] != 0xE9) {
        ESP_LOGE(TAG, "Firmware inválido: primer byte 0x%02X (esperado 0xE9)", (uint8_t)buf[0]);
        esp_ota_end(ota_handle);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Archivo OTA inválido");
        return ESP_FAIL;
      }
      first_chunk_checked = true;
      ESP_LOGI(TAG, "Encabezado OTA válido detectado (0xE9)");
    }

    if (esp_ota_write(ota_handle, buf, received) != ESP_OK) {
      ESP_LOGE(TAG, "Error escribiendo OTA");
      esp_ota_end(ota_handle);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error escribiendo flash");
      return ESP_FAIL;
    }

    total += received;
    remaining -= received;

    if (total % (64 * 1024) < sizeof(buf)) { // cada ~64KB
      ESP_LOGI(TAG, "Progreso OTA: %d / %d bytes (%.1f%%)",
               total, req->content_len,
               (req->content_len > 0) ? (100.0 * total / req->content_len) : 0.0);
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }

  if (esp_ota_end(ota_handle) != ESP_OK) {
    ESP_LOGE(TAG, "Error finalizando OTA");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Fallo esp_ota_end");
    return ESP_FAIL;
  }

  if (esp_ota_set_boot_partition(update_partition) != ESP_OK) {
    ESP_LOGE(TAG, "Error configurando partición OTA");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Fallo esp_ota_set_boot_partition");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "OTA completada correctamente, %d bytes recibidos", total);
  httpd_resp_sendstr(req, "Actualización completada. Reiniciando...");
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
  return ESP_OK;
}

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
    "<button onclick='upload()'>Subir y actualizar</button>"
    "<pre id='log'></pre>"
    "</div>"
    "<script>"
    "async function upload(){"
      "const file=document.getElementById('firmware').files[0];"
      "const log=document.getElementById('log');"
      "if(!file){alert('Selecciona un archivo .bin');return;}"
      "log.textContent='Subiendo '+file.name+' ('+file.size+' bytes)...';"
      "const res=await fetch('/update',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:file});"
      "if(res.ok){log.textContent+='\\nActualización completada. Reiniciando...';}"
      "else{log.textContent+='\\nError en la actualización: '+res.status;}"
    "}"
    "</script>"
    "</body></html>";

  httpd_resp_set_type(req, "text/html");
  httpd_resp_sendstr(req, html);
  return ESP_OK;
}

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

  esp_err_t res = httpd_register_uri_handler(server, &update_get);
  if (res != ESP_OK) return res;
  return httpd_register_uri_handler(server, &update_post);
}

