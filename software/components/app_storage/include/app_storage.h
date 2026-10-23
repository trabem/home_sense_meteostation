#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NVS-backed persistent state.
 *
 * Boot counter and small device state only; NVS is not a log database.
 * Wi-Fi credentials are managed by the provisioning manager itself.
 */

/** nvs_flash_init() with erase-and-retry fallback for full/corrupt pages. */
esp_err_t app_storage_init(void);

/** Increments the persistent boot counter and returns the new value. */
esp_err_t app_storage_next_boot_count(uint32_t *boot_count);

#ifdef __cplusplus
}
#endif
