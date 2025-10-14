#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <stdint.h>
#include "esp_err.h"

/**
 * Callback para RPC. 
 * method: nombre del método (p. ej. "setInterval")
 * params: contenido JSON de params (cadena terminada en '\0')
 */
typedef void (*mqtt_rpc_cb_t)(const char *method, const char *params);

/**
 * Inicializa MQTT y registra internamente el handler.
 * broker_url: "mqtt://IP:PORT" o similar
 * token: token de ThingsBoard (user)
 */
void mqtt_manager_init(const char *broker_url, const char *token);

/**
 * Publica telemetría de luz (ya existente).
 */
void mqtt_manager_publish_light(uint8_t light_level);

/**
 * Publica temperatura y humedad (nuevo).
 * Publica en topic "v1/devices/me/telemetry" un JSON {"temperature":..., "humidity": ...}
 */
void mqtt_manager_publish_env(float temperature, float humidity);

/**
 * Registra callback para RPCs recibidos por MQTT (topic v1/devices/me/rpc/request/+)
 */
void mqtt_manager_set_rpc_callback(mqtt_rpc_cb_t cb);
void mqtt_manager_disconnect(void);
void mqtt_manager_reconnect(void);


#endif // MQTT_MANAGER_H

