#include "lightning_service.h"

#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "AS3935_SVC";

struct lightning_service {
    as3935_handle_t as3935;
};

esp_err_t lightning_service_init(lightning_service_handle_t *out_handle,
                                 const lightning_service_config_t *config)
{
    ESP_RETURN_ON_FALSE(out_handle != NULL && config != NULL && config->as3935 != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "handle/config required");

    struct lightning_service *svc = calloc(1, sizeof(*svc));
    ESP_RETURN_ON_FALSE(svc != NULL, ESP_ERR_NO_MEM, TAG, "no mem");

    svc->as3935 = config->as3935;
    *out_handle = svc;
    return ESP_OK;
}

esp_err_t lightning_service_process_irq(lightning_service_handle_t handle,
                                        lightning_event_t *event,
                                        bool *is_lightning)
{
    ESP_RETURN_ON_FALSE(handle != NULL && event != NULL && is_lightning != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "args required");

    *is_lightning = false;

    as3935_irq_reason_t reason = AS3935_IRQ_NONE;
    ESP_RETURN_ON_ERROR(as3935_get_interrupt_reason(handle->as3935, &reason), TAG,
                        "irq reason read failed");

    if (reason != AS3935_IRQ_LIGHTNING) {
        ESP_LOGI(TAG, "wake IRQ source is not lightning (reason=0x%02x), no event",
                 (unsigned)reason);
        as3935_clear_statistics(handle->as3935);
        return ESP_OK;
    }

    *is_lightning = true;

    ESP_RETURN_ON_ERROR(as3935_read_lightning_distance(handle->as3935, &event->distance_km),
                        TAG, "distance read failed");

    uint32_t energy = 0;
    if (as3935_read_lightning_energy(handle->as3935, &energy) == ESP_OK) {
        event->energy = energy;
        event->has_energy = true;
    }

    ESP_LOGI(TAG, "lightning: distance=%u km energy=%lu", event->distance_km,
             (unsigned long)event->energy);

    /* Acknowledge so the IRQ line re-arms before the next deep sleep. */
    ESP_RETURN_ON_ERROR(as3935_clear_statistics(handle->as3935), TAG, "clear stats failed");
    return ESP_OK;
}

esp_err_t lightning_service_deinit(lightning_service_handle_t handle)
{
    free(handle);
    return ESP_OK;
}
