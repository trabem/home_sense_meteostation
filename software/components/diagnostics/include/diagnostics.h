#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Failure counters.
 *
 * Volatile per boot cycle by design: they answer "what went wrong during
 * this active window" and are reported with telemetry. boot_count itself is
 * persisted in NVS by app_storage.
 */

typedef struct {
    uint32_t boot_count;
    uint32_t wifi_failures;
    uint32_t mqtt_failures;
    uint32_t lora_failures;
    uint32_t sensor_failures;
} diagnostics_t;

typedef enum {
    DIAG_WIFI_FAILURE,
    DIAG_MQTT_FAILURE,
    DIAG_LORA_FAILURE,
    DIAG_SENSOR_FAILURE,
} diag_counter_t;

void diagnostics_init(uint32_t boot_count);
void diagnostics_increment(diag_counter_t counter);
void diagnostics_get(diagnostics_t *out);

#ifdef __cplusplus
}
#endif
