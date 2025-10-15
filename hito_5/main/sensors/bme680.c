#include "bme680.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"

#define TAG "BME680"

#define BME680_CHIP_ID_REG      0xD0
#define BME680_RESET_REG        0xE0
#define BME680_CTRL_HUM         0x72
#define BME680_CTRL_MEAS        0x74
#define BME680_CONFIG           0x75
#define BME680_PRESS_MSB        0x1F

#define BME680_CHIP_ID          0x61
#define BME680_SOFT_RESET_CMD   0xB6

#define I2C_TIMEOUT_MS          1000

// ======================================================
// Funciones internas
// ======================================================
static esp_err_t bme680_write_byte(i2c_port_t i2c_port, uint8_t addr, uint8_t reg, uint8_t value) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_write_byte(cmd, value, true);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
  i2c_cmd_link_delete(cmd);
  return ret;
}

static esp_err_t bme680_read_bytes(i2c_port_t i2c_port, uint8_t addr, uint8_t reg, uint8_t *data, size_t len) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
  if (len > 1) {
    i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
  }
  i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
  i2c_cmd_link_delete(cmd);
  return ret;
}

// ======================================================
// Inicialización
// ======================================================
esp_err_t bme680_init(i2c_port_t i2c_port, uint8_t i2c_address) {
  uint8_t chip_id = 0;
  esp_err_t ret = bme680_read_bytes(i2c_port, i2c_address, BME680_CHIP_ID_REG, &chip_id, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "No se pudo leer el ID del chip");
    return ret;
  }

  if (chip_id != BME680_CHIP_ID) {
    ESP_LOGE(TAG, "Chip ID no válido: 0x%02X", chip_id);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "BME680 detectado correctamente (ID 0x%02X)", chip_id);

  // Reset
  bme680_write_byte(i2c_port, i2c_address, BME680_RESET_REG, BME680_SOFT_RESET_CMD);
  vTaskDelay(pdMS_TO_TICKS(10));

  // Configuración básica
  bme680_write_byte(i2c_port, i2c_address, BME680_CTRL_HUM, 0x01);  // oversampling 1x humedad
  bme680_write_byte(i2c_port, i2c_address, BME680_CTRL_MEAS, 0x24); // osrs_t=1x, osrs_p=1x, modo forzado
  bme680_write_byte(i2c_port, i2c_address, BME680_CONFIG, 0x00);    // sin filtro IIR

  return ESP_OK;
}

// ======================================================
// Lectura de datos
// ======================================================
esp_err_t bme680_read_data(i2c_port_t i2c_port, uint8_t i2c_address, bme680_data_t *data) {
  uint8_t raw[8] = {0};
  esp_err_t ret = bme680_read_bytes(i2c_port, i2c_address, BME680_PRESS_MSB, raw, 8);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error leyendo datos del sensor");
    return ret;
  }

  // Datos crudos (simplificados, sin compensación real)
  int32_t adc_press = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | ((raw[2] >> 4) & 0x0F);
  int32_t adc_temp  = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | ((raw[5] >> 4) & 0x0F);
  int32_t adc_hum   = ((int32_t)raw[6] << 8)  | raw[7];

  data->temperature = adc_temp / 100.0f;
  data->pressure = adc_press / 100.0f;
  data->humidity = adc_hum / 1024.0f;
  data->gas_resistance = 0.0f;

  return ESP_OK;
}

// ======================================================
// Log
// ======================================================
void bme680_print_data(const bme680_data_t *data) {
  ESP_LOGI(TAG, "Temp: %.2f °C | Hum: %.2f %% | Pres: %.2f hPa | Gas: %.2f Ω",
           data->temperature, data->humidity, data->pressure, data->gas_resistance);
}

