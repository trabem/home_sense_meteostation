#include "lora_transport.h"

#include <stdlib.h>

#include "esp_log.h"

#define TAG "LORA"

#define LORA_MAX_PAYLOAD 255

typedef struct {
    transport_t base;
    lora_transport_config_t config;
} lora_transport_t;

static esp_err_t lora_open(transport_t *self)
{
    lora_transport_t *transport = (lora_transport_t *)self;
    esp_err_t err = sx1262_init(transport->config.radio);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "radio init/wake failed: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t lora_send(transport_t *self, message_type_t type,
                           const uint8_t *payload, size_t payload_size)
{
    (void)type; /* already encoded in the binary payload by telemetry_codec */
    lora_transport_t *transport = (lora_transport_t *)self;
    if (payload == NULL || payload_size == 0 || payload_size > LORA_MAX_PAYLOAD) {
        ESP_LOGE(TAG, "invalid payload size %u", (unsigned)payload_size);
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = sx1262_send(transport->config.radio, payload, payload_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LoRa TX failed: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t lora_close(transport_t *self)
{
    lora_transport_t *transport = (lora_transport_t *)self;
    return sx1262_sleep(transport->config.radio);
}

static const struct transport_ops s_lora_ops = {
    .open = lora_open,
    .send = lora_send,
    .close = lora_close,
};

esp_err_t lora_transport_create(const lora_transport_config_t *config,
                                transport_t **out_transport)
{
    if (config == NULL || out_transport == NULL || config->radio == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    lora_transport_t *transport = calloc(1, sizeof(*transport));
    if (transport == NULL) {
        return ESP_ERR_NO_MEM;
    }
    transport->base.ops = &s_lora_ops;
    transport->config = *config;
    *out_transport = &transport->base;
    ESP_LOGI(TAG, "LoRa transport created");
    return ESP_OK;
}
