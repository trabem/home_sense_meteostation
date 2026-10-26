#pragma once

#include "bme280.h"
#include "esp_err.h"
#include "ina226.h"
#include "telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Measurement subsystem.
 *
 * Initialized only for a periodic measurement cycle, never for a lightning
 * wake. One failed sensor does not block telemetry: the corresponding
 * validity flag is cleared and the rest is still reported.
 */

typedef struct sensor_manager *sensor_manager_handle_t;

typedef struct {
    bme280_handle_t bme280;
    ina226_handle_t battery_ina;
    ina226_handle_t solar_ina;
} sensor_manager_config_t;

esp_err_t sensor_manager_init(sensor_manager_handle_t *out_handle,
                              const sensor_manager_config_t *config);

/**
 * Run one measurement cycle: BME280 forced measurement, both INA226 single
 * shots (each powered down right after), charger state via board.
 * Fills telemetry->*_valid flags. Returns ESP_OK if at least one source
 * succeeded.
 */
esp_err_t sensor_manager_measure(sensor_manager_handle_t handle,
                                 telemetry_t *telemetry);

esp_err_t sensor_manager_deinit(sensor_manager_handle_t handle);

#ifdef __cplusplus
}
#endif
