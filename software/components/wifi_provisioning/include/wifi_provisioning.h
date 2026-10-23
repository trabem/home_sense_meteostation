#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * BLE Wi-Fi provisioning via the standard ESP-IDF provisioning manager
 * (network_provisioning component, protocomm over BLE).
 *
 * Started only when credentials are missing (or after an explicit reset),
 * never on every boot. Service name: METEO_<last 3 MAC bytes>, e.g.
 * METEO_A17C42. Proof-of-possession is derived per-device from the factory
 * MAC (never one hardcoded value for all units).
 */

/**
 * Run BLE provisioning and block until credentials arrive or timeout_ms
 * elapses. On success provisioning is stopped, BLE resources are released
 * and credentials are stored in NVS by the provisioning manager.
 */
esp_err_t wifi_provisioning_run(uint32_t timeout_ms);

/** Erase stored Wi-Fi credentials (development / future control interface). */
esp_err_t wifi_provisioning_reset(void);

#ifdef __cplusplus
}
#endif
