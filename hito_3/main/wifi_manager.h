#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H
#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 64


#include <stdbool.h>
#include "esp_http_server.h"

/**
 * @brief Inicializa el gestor WiFi.
 * - Monta SPIFFS
 * - Crea interfaces STA/AP
 * - Arranca en modo APSTA
 * - Si hay credenciales guardadas, intenta conectar
 */
void wifi_manager_init(void);

/**
 * @brief Devuelve true si el ESP32 está conectado a una red WiFi.
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Inicia el servidor web con las rutas:
 *  - "/"     → redirige a /scan
 *  - "/scan" → muestra redes WiFi disponibles
 *  - "/config" (POST) → guarda credenciales y reinicia
 */
httpd_handle_t wifi_manager_start_webserver(void);

#endif // WIFI_MANAGER_H

