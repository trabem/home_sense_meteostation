#pragma once

#include "esp_err.h"
#include "sx1262.h"
#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * LoRa transport: TX-only, one shot per message (TZ 23).
 *
 * open()  -> wake SX1262 from sleep and (re)apply radio configuration
 * send()  -> bounded blocking TX (message type is already encoded in the
 *            binary payload by telemetry_codec)
 * close() -> SX1262 SetSleep
 */

typedef struct {
    sx1262_handle_t radio;
} lora_transport_config_t;

esp_err_t lora_transport_create(const lora_transport_config_t *config,
                                transport_t **out_transport);

#ifdef __cplusplus
}
#endif
