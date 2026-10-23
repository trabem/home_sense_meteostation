#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Single shared I2C master bus.
 *
 * The bus is a shared resource for DS3231, INA226 x2, PCA9536, BME280 and
 * AS3935. The ESP-IDF i2c_master driver serializes access internally, so one
 * bus handle can be handed to every device driver.
 */

typedef struct {
    gpio_num_t sda_gpio;
    gpio_num_t scl_gpio;
    uint32_t frequency_hz;   /* CONFIG_METEO_I2C_FREQUENCY_HZ */
} i2c_bus_config_t;

esp_err_t i2c_bus_init(const i2c_bus_config_t *config);

/** Valid after i2c_bus_init() succeeded. */
i2c_master_bus_handle_t i2c_bus_get_handle(void);

esp_err_t i2c_bus_deinit(void);

#ifdef __cplusplus
}
#endif
