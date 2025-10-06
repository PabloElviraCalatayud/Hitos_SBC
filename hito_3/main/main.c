#include "wifi_manager.h"
#include "ldr_monitor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void) {
    wifi_manager_init();

    // Espera hasta que se conecte al WiFi
    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Cuando hay WiFi -> arrancamos monitorización
    ldr_monitor_start();
}

