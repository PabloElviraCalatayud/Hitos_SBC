#include "ota_manager.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_http_server.h"

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

  while (remaining > 0) {
    int received = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
    if (received <= 0) {
      ESP_LOGE(TAG, "Error recibiendo firmware");
      esp_ota_end(ota_handle);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error recibiendo datos");
      return ESP_FAIL;
    }
    if (esp_ota_write(ota_handle, buf, received) != ESP_OK) {
      ESP_LOGE(TAG, "Error escribiendo OTA");
      esp_ota_end(ota_handle);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error escribiendo flash");
      return ESP_FAIL;
    }
    total += received;
    remaining -= received;
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

  ESP_LOGI(TAG, "OTA completada, %d bytes recibidos", total);
  httpd_resp_sendstr(req, "Actualización completada. Reiniciando...");
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
  return ESP_OK;
}

static esp_err_t ota_get_handler(httpd_req_t *req) {
  const char *html =
    "<h2>Actualizar firmware OTA</h2>"
    "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<input type='file' name='firmware'>"
    "<input type='submit' value='Actualizar'>"
    "</form>";
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

