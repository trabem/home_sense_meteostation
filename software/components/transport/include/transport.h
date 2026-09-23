#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Transport abstraction (TZ 15).
 *
 * The application layer only sees this interface; it does not know whether
 * bytes leave via MQTT/TLS or an SX1262. Payloads are produced by
 * telemetry_codec, topics/frames by the concrete transport.
 */

typedef struct transport transport_t;

struct transport_ops {
    esp_err_t (*open)(transport_t *self);
    esp_err_t (*send)(transport_t *self, message_type_t type,
                      const uint8_t *payload, size_t payload_size);
    esp_err_t (*close)(transport_t *self);
};

struct transport {
    const struct transport_ops *ops;
};

static inline esp_err_t transport_open(transport_t *t)
{
    return t->ops->open(t);
}

static inline esp_err_t transport_send(transport_t *t, message_type_t type,
                                       const uint8_t *payload, size_t payload_size)
{
    return t->ops->send(t, type, payload, payload_size);
}

static inline esp_err_t transport_close(transport_t *t)
{
    return t->ops->close(t);
}

#ifdef __cplusplus
}
#endif
