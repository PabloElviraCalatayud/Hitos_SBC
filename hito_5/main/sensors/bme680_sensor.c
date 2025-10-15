#include "bme680_sensor.h"
#include "bme68x.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include <string.h>

#define I2C_MASTER_SCL_IO           22
#define I2C_MASTER_SDA_IO           21
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          100000
#define I2C_MASTER_TX_BUF_DISABLE   0
#define I2C_MASTER_RX_BUF_DISABLE   0

#define BME68X_I2C_ADDR             BME68X_I2C_ADDR_HIGH

static const char *TAG = "BME680_SENSOR";
static struct bme68x_dev bme_dev;
static uint8_t dev_addr = BME68X_I2C_ADDR;

// -----------------------------------------------------------------------------
// Funciones auxiliares requeridas por el driver oficial de Bosch
// -----------------------------------------------------------------------------
static int8_t i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr) {
  uint8_t dev_addr = *(uint8_t *)intf_ptr;
  esp_err_t err = i2c_master_write_read_device(I2C_MASTER_NUM, dev_addr, &reg_addr, 1, reg_data, len, pdMS_TO_TICKS(1000));
  return (err == ESP_OK) ? BME68X_OK : BME68X_E_COM_FAIL;
}

static int8_t i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr) {
  uint8_t dev_addr = *(uint8_t *)intf_ptr;
  uint8_t buf[len + 1];
  buf[0] = reg_addr;
  memcpy(&buf[1], reg_data, len);
  esp_err_t err = i2c_master_write_to_device(I2C_MASTER_NUM, dev_addr, buf, len + 1, pdMS_TO_TICKS(1000));
  return (err == ESP_OK) ? BME68X_OK : BME68X_E_COM_FAIL;
}

static void delay_us(uint32_t period, void *intf_ptr) {
  esp_rom_delay_us(period);
}


// -----------------------------------------------------------------------------
// Inicialización del bus I2C
// -----------------------------------------------------------------------------
static esp_err_t i2c_master_init(void) {
  i2c_config_t conf = {
    .mode = I2C_MODE_MASTER,
    .sda_io_num = I2C_MASTER_SDA_IO,
    .scl_io_num = I2C_MASTER_SCL_IO,
    .sda_pullup_en = GPIO_PULLUP_ENABLE,
    .scl_pullup_en = GPIO_PULLUP_ENABLE,
    .master.clk_speed = I2C_MASTER_FREQ_HZ,
  };

  esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
  if (err != ESP_OK) return err;

  return i2c_driver_install(I2C_MASTER_NUM, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}

// -----------------------------------------------------------------------------
// Inicialización del BME680
// -----------------------------------------------------------------------------
esp_err_t bme680_init_sensor(void) {
  ESP_LOGI(TAG, "Inicializando BME680...");

  ESP_ERROR_CHECK(i2c_master_init());

  bme_dev.intf = BME68X_I2C_INTF;
  bme_dev.read = i2c_read;
  bme_dev.write = i2c_write;
  bme_dev.delay_us = delay_us;
  bme_dev.intf_ptr = &dev_addr;

  int8_t rslt = bme68x_init(&bme_dev);
  if (rslt != BME68X_OK) {
    ESP_LOGE(TAG, "Error en bme68x_init (%d)", rslt);
    return ESP_FAIL;
  }

  struct bme68x_conf conf = {
    .filter = BME68X_FILTER_OFF,
    .odr = BME68X_ODR_NONE,
    .os_hum = BME68X_OS_2X,
    .os_pres = BME68X_OS_4X,
    .os_temp = BME68X_OS_8X
  };
  bme68x_set_conf(&conf, &bme_dev);

  struct bme68x_heatr_conf heatr_conf = {
    .enable = BME68X_ENABLE,
    .heatr_temp = 320,
    .heatr_dur = 150
  };
  bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr_conf, &bme_dev);

  ESP_LOGI(TAG, "BME680 inicializado correctamente");
  return ESP_OK;
}

// -----------------------------------------------------------------------------
// Lectura de datos del BME680
// -----------------------------------------------------------------------------
esp_err_t bme680_read_data(bme680_data_t *data) {
  if (!data) return ESP_ERR_INVALID_ARG;

  bme68x_set_op_mode(BME68X_FORCED_MODE, &bme_dev);
  bme_dev.delay_us(100000, bme_dev.intf_ptr); // 100 ms aprox

  struct bme68x_data sensor_data;
  uint8_t n_fields;
  int8_t rslt = bme68x_get_data(BME68X_FORCED_MODE, &sensor_data, &n_fields, &bme_dev);

  if (rslt != BME68X_OK || n_fields == 0) {
    ESP_LOGW(TAG, "No se pudieron obtener datos válidos (%d)", rslt);
    return ESP_FAIL;
  }

  data->temperature = sensor_data.temperature;
  data->humidity = sensor_data.humidity;
  data->pressure = sensor_data.pressure / 100.0f;      // Pa → hPa
  data->gas_resistance = sensor_data.gas_resistance / 1000.0f; // Ω → kΩ

  return ESP_OK;
}

