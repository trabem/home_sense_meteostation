#include "transport_factory.h"

#include <stdlib.h>

#include "esp_log.h"

#define TAG "TRANSPORT"

esp_err_t transport_create(transport_mode_t mode,
                           const transport_factory_config_t *config,
                           transport_t **out_transport)
{
    if (config == NULL || out_transport == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (mode) {
    case TRANSPORT_MODE_WIFI:
        return mqtt_transport_create(&config->mqtt, out_transport);
    case TRANSPORT_MODE_LORA:
        return lora_transport_create(&config->lora, out_transport);
    default:
        ESP_LOGE(TAG, "unknown transport mode %d", mode);
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t transport_destroy(transport_t *transport)
{
    if (transport == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    free(transport);
    return ESP_OK;
}
