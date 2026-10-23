#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Generic INA226 power monitor driver.
 *
 * One driver, two instances: battery (0x44, Rshunt 0.020 ohm) and solar
 * (0x40, Rshunt 0.020 ohm). Between measurements devices are put into
 * power-down mode to save energy.
 */

typedef struct ina226 *ina226_handle_t;

typedef struct {
    uint8_t i2c_address;
    float shunt_resistance_ohm;
    float max_expected_current_a;
} ina226_config_t;

esp_err_t ina226_create(i2c_master_bus_handle_t bus,
                        const ina226_config_t *config,
                        ina226_handle_t *out_handle);

/**
 * Trigger a single shot conversion, wait for completion (bounded) and read
 * bus voltage / current / power. Leaves the device running; the caller must
 * follow with ina226_power_down().
 */
esp_err_t ina226_read(ina226_handle_t handle, power_measurement_t *measurement);

esp_err_t ina226_power_down(ina226_handle_t handle);

esp_err_t ina226_destroy(ina226_handle_t handle);

#ifdef __cplusplus
}
#endif
