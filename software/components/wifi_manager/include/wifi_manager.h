#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Wi-Fi station lifecycle (TZ 16, 18).
 *
 * Wi-Fi is started only for a transmission window and stopped right after.
 * Connection attempts are bounded: timeout + limited retries, never an
 * infinite loop.
 */

/** One-time init of netif/wifi driver. Safe to call once per boot. */
esp_err_t wifi_manager_init(void);

/** True when station credentials are stored (NVS/default config). */
esp_err_t wifi_manager_has_credentials(bool *has_credentials);

/**
 * Connect to the configured AP. Bounded by timeout_ms and max_retries.
 * Posts METEO_EVENT_WIFI_CONNECTED / METEO_EVENT_WIFI_FAILED.
 * Returns ESP_ERR_TIMEOUT on failure.
 */
esp_err_t wifi_manager_connect(uint32_t timeout_ms, int max_retries);

/** Disconnect and fully stop/deinit the Wi-Fi driver (low power). */
esp_err_t wifi_manager_stop(void);

#ifdef __cplusplus
}
#endif
