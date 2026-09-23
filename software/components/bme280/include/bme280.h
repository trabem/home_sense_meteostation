#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * BME280 environmental sensor, forced mode only (TZ 8).
 *
 * CSB=3V3, SDO=GND -> I2C address 0x76. Each measurement cycle:
 * trigger forced measurement -> wait completion -> read T/P/RH -> sleep.
 * The sensor never stays in normal (continuous) mode.
 */

#define BME280_I2C_ADDRESS_DEFAULT  0x76

typedef struct bme280 *bme280_handle_t;

typedef struct {
    uint8_t i2c_address;
} bme280_config_t;

#define BME280_CONFIG_DEFAULT() { .i2c_address = BME280_I2C_ADDRESS_DEFAULT }

/** Creates the device, verifies the chip ID and reads calibration data. */
esp_err_t bme280_create(i2c_master_bus_handle_t bus,
                        const bme280_config_t *config,
                        bme280_handle_t *out_handle);

/**
 * Run one forced measurement with oversampling x1 and return compensated
 * values. Waits for conversion with a bounded timeout, then puts the sensor
 * back to sleep.
 */
esp_err_t bme280_read_forced(bme280_handle_t handle,
                             environment_measurement_t *measurement);

esp_err_t bme280_sleep(bme280_handle_t handle);

esp_err_t bme280_destroy(bme280_handle_t handle);

#ifdef __cplusplus
}
#endif
