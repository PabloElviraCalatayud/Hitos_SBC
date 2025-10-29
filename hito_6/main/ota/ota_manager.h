#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t ota_register_endpoints(httpd_handle_t server);

// NUEVAS funciones
bool ota_has_pending_update(void);
void ota_apply_pending_update(void);

