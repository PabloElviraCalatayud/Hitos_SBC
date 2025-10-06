idf_component_register(
  SRCS
    "main.c"
    "drivers/adc_driver.c"
    "sensors/ldr_sensor.c"
    "utils/math_utils.c"
  INCLUDE_DIRS
    "."
    "drivers"
    "sensors"
    "utils"
)

