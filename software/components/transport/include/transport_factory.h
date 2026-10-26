#pragma once

#include "esp_err.h"
#include "lora_transport.h"
#include "mqtt_transport.h"
#include "telemetry.h"
#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Creates the transport selected by the J4 mode.
 *
 *   TRANSPORT_MODE_WIFI -> mqtt_transport
 *   TRANSPORT_MODE_LORA -> lora_transport
 */

typedef struct {
    mqtt_transport_config_t mqtt;   /* used when mode == TRANSPORT_MODE_WIFI */
    lora_transport_config_t lora;   /* used when mode == TRANSPORT_MODE_LORA */
} transport_factory_config_t;

esp_err_t transport_create(transport_mode_t mode,
                           const transport_factory_config_t *config,
                           transport_t **out_transport);

/** Frees the transport created by transport_create(). */
esp_err_t transport_destroy(transport_t *transport);

#ifdef __cplusplus
}
#endif
