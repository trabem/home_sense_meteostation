#pragma once

#include <stdbool.h>

#include "as3935.h"
#include "esp_err.h"
#include "telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Lightning event processing (TZ 31, 49, 52).
 *
 * Minimal path after a WAKE_IRQ wake: read the AS3935 interrupt reason,
 * and only for a real lightning interrupt collect distance/energy.
 */

typedef struct lightning_service *lightning_service_handle_t;

typedef struct {
    as3935_handle_t as3935;
} lightning_service_config_t;

esp_err_t lightning_service_init(lightning_service_handle_t *out_handle,
                                 const lightning_service_config_t *config);

/**
 * Reads the AS3935 interrupt reason. *is_lightning is set accordingly;
 * event fields are filled only for a genuine lightning interrupt
 * (distance always, energy when readable). Statistics/interrupt are
 * acknowledged afterwards so the IRQ line can re-arm.
 */
esp_err_t lightning_service_process_irq(lightning_service_handle_t handle,
                                        lightning_event_t *event,
                                        bool *is_lightning);

esp_err_t lightning_service_deinit(lightning_service_handle_t handle);

#ifdef __cplusplus
}
#endif
