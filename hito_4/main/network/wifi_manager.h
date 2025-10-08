#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>

#define WIFI_MANAGER_MAX_SSID_LEN 64
#define WIFI_MANAGER_MAX_PASS_LEN 64

/**
 * @brief Inicializa el sistema WiFi.
 * 
 * - Monta SPIFFS.
 * - Crea modo AP+STA.
 * - Si hay credenciales guardadas, intenta conectar automáticamente.
 * - Si no hay credenciales, abre un portal WiFi para configurar.
 */
void wifi_manager_init(void);

/**
 * @brief Indica si el ESP32 está conectado a una red WiFi.
 * 
 * @return true si está conectado, false en caso contrario.
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Guarda credenciales en SPIFFS.
 * 
 * @param ssid SSID de la red.
 * @param pass Contraseña de la red.
 * @return true si se guardaron correctamente.
 */
bool save_wifi_credentials(const char *ssid, const char *pass);

/**
 * @brief Carga credenciales guardadas desde SPIFFS.
 * 
 * @param ssid Buffer para almacenar el SSID.
 * @param pass Buffer para almacenar la contraseña.
 * @return true si se cargaron correctamente.
 */
bool load_wifi_credentials(char *ssid, char *pass);

#endif // WIFI_MANAGER_H

