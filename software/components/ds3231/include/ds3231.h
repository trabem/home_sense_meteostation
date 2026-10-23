#pragma once

#include <time.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DS3231 RTC - canonical hardware clock.
 *
 * The CR2032 keeps time across main power loss. Application code works with
 * unix timestamps; this driver only converts to/from DS3231 BCD registers.
 */

#define DS3231_I2C_ADDRESS_DEFAULT  0x68

typedef struct ds3231 *ds3231_handle_t;

typedef struct {
    uint8_t i2c_address;
} ds3231_config_t;

#define DS3231_CONFIG_DEFAULT() { .i2c_address = DS3231_I2C_ADDRESS_DEFAULT }

esp_err_t ds3231_create(i2c_master_bus_handle_t bus,
                        const ds3231_config_t *config,
                        ds3231_handle_t *out_handle);

esp_err_t ds3231_get_time(ds3231_handle_t handle, struct tm *time);
esp_err_t ds3231_set_time(ds3231_handle_t handle, const struct tm *time);

/** Convenience wrappers around get/set time (UTC). */
esp_err_t ds3231_get_unix_time(ds3231_handle_t handle, int64_t *timestamp);
esp_err_t ds3231_set_unix_time(ds3231_handle_t handle, int64_t timestamp);

esp_err_t ds3231_destroy(ds3231_handle_t handle);

#ifdef __cplusplus
}
#endif
