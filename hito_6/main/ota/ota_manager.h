#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

// Registra los endpoints de OTA en el servidor HTTP existente
esp_err_t ota_register_endpoints(httpd_handle_t server);

