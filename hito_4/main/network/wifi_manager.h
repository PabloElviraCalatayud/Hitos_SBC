#pragma once
#include <stdbool.h>
#include "esp_http_server.h"

#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 64

void wifi_manager_init(void);
bool wifi_manager_is_connected(void);
httpd_handle_t wifi_manager_start_webserver(void);

