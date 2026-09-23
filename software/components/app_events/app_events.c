#include "app_events.h"

#include "esp_check.h"
#include "esp_log.h"

ESP_EVENT_DEFINE_BASE(METEO_EVENT);

static const char *TAG = "EVENTS";

esp_err_t app_events_init(void)
{
    const esp_err_t err = esp_event_loop_create_default();
    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;  /* already created */
    }
    ESP_RETURN_ON_ERROR(err, TAG, "event loop create failed");
    return ESP_OK;
}
