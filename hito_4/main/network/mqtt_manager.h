#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <stdint.h>

void mqtt_manager_init(const char *broker_url, const char *token);
void mqtt_manager_publish_light(uint8_t light_level);

#endif

