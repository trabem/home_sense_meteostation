#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * MQTT transport over ESP-MQTT.
 *
 * open()  -> start client, wait for MQTT_EVENT_CONNECTED (bounded)
 * send()  -> publish JSON to meteostation/<device_id>/{telemetry|events|status}
 *            with configured QoS and wait for delivery confirmation
 *            (MQTT_EVENT_PUBLISHED for QoS>0) or timeout
 * close() -> disconnect and stop the client
 *
 * Wi-Fi must already be connected before open(). TLS (mqtts://) is
 * mandatory for production; certificate validation can only be disabled via
 * an explicit development flag (CONFIG_METEO_MQTT_TLS_SKIP_VERIFY).
 */

typedef struct {
    const char *broker_uri;     /* mqtts://... for production */
    const char *username;       /* NULL/empty = anonymous */
    const char *password;       /* NULL/empty = none */
    const char *device_id;      /* used in topics and client id */
    const char *root_topic;     /* e.g. "meteostation" */
    int qos;
    uint32_t connect_timeout_ms;
    uint32_t publish_timeout_ms;
    bool tls_skip_verify;       /* development only */
    const char *ca_cert_pem;    /* optional custom CA, NULL = default bundle */
} mqtt_transport_config_t;

esp_err_t mqtt_transport_create(const mqtt_transport_config_t *config,
                                transport_t **out_transport);

#ifdef __cplusplus
}
#endif
