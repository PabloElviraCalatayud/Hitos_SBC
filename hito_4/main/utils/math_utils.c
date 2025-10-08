#include "math_utils.h"
#include <math.h>

uint8_t calculate_light_level(float resistance) {
  const float R_DARK = 100000.0f;
  const float R_LIGHT = 1000.0f;

  float level = 100.0f * (log10f(R_DARK / resistance) / log10f(R_DARK / R_LIGHT));

  if (level < 0.0f) {
    level = 0.0f;
  }
  if (level > 100.0f) {
    level = 100.0f;
  }

  return (uint8_t)roundf(level);
}
