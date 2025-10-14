#include "dht_sensor.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"

static const char *TAG = "DHT_SENSOR";
static gpio_num_t s_dht_pin;

#define DHT_TIMEOUT_US 1000

static int dht_wait_level(gpio_num_t pin, int level, uint32_t timeout) {
  int t = 0;
  while (gpio_get_level(pin) == level) {
    if (t++ > timeout) return -1;
    esp_rom_delay_us(1);
  }
  return t;
}

esp_err_t dht_sensor_init(gpio_num_t pin) {
  s_dht_pin = pin;
  gpio_set_direction(s_dht_pin, GPIO_MODE_INPUT_OUTPUT_OD);
  gpio_set_pull_mode(s_dht_pin, GPIO_PULLUP_ONLY);
  ESP_LOGI(TAG, "DHT inicializado en GPIO %d", pin);
  return ESP_OK;
}

esp_err_t dht_sensor_read(dht_data_t *data) {
  if (!data) return ESP_ERR_INVALID_ARG;

  uint8_t raw[5] = {0};

  // Start signal
  gpio_set_direction(s_dht_pin, GPIO_MODE_OUTPUT);
  gpio_set_level(s_dht_pin, 0);
  esp_rom_delay_us(20000); // 20 ms
  gpio_set_level(s_dht_pin, 1);
  esp_rom_delay_us(40);
  gpio_set_direction(s_dht_pin, GPIO_MODE_INPUT);

  // Sensor response
  if (dht_wait_level(s_dht_pin, 1, DHT_TIMEOUT_US) < 0) return ESP_FAIL;
  if (dht_wait_level(s_dht_pin, 0, DHT_TIMEOUT_US) < 0) return ESP_FAIL;

  // Read 40 bits
  for (int i = 0; i < 40; i++) {
    if (dht_wait_level(s_dht_pin, 1, DHT_TIMEOUT_US) < 0) return ESP_FAIL;
    int len = dht_wait_level(s_dht_pin, 0, DHT_TIMEOUT_US);
    if (len < 0) return ESP_FAIL;
    raw[i / 8] <<= 1;
    if (len > 40) raw[i / 8] |= 1; // long pulse -> bit 1
  }

  // Checksum
  uint8_t sum = raw[0] + raw[1] + raw[2] + raw[3];
  if ((sum & 0xFF) != raw[4]) {
    ESP_LOGW(TAG, "Checksum DHT error");
    return ESP_FAIL;
  }

  // DHT11: raw[0] humidity integer, raw[2] temperature integer
  data->humidity = (float)raw[0];
  data->temperature = (float)raw[2];
  ESP_LOGI(TAG, "DHT lectura -> Temp: %.1f C, Hum: %.1f %%", data->temperature, data->humidity);

  return ESP_OK;
}

