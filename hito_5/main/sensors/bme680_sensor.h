#ifndef BME680_SENSOR_H
#define BME680_SENSOR_H

#include "esp_err.h"

// --- Datos medidos por el BME680 ---
typedef struct {
  float temperature;     // °C
  float humidity;        // %
  float pressure;        // hPa
  float gas_resistance;  // kΩ
} bme680_data_t;

// --- Inicialización del sensor ---
esp_err_t bme680_init_sensor(void);

// --- Lectura de datos ---
esp_err_t bme680_read_data(bme680_data_t *data);

#endif // BME680_SENSOR_H

