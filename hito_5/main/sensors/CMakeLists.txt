idf_component_register(
  SRCS "bme680_sensor.c" "ldr_sensor.c"
  INCLUDE_DIRS "."
  REQUIRES bme68x driver esp_common
)

