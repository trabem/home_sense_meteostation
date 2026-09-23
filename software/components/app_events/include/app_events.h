#pragma once

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Application-wide asynchronous events on the standard esp_event loop
 * (TZ 34).
 */

ESP_EVENT_DECLARE_BASE(METEO_EVENT);

enum {
    METEO_EVENT_WIFI_CONNECTED,
    METEO_EVENT_WIFI_FAILED,
    METEO_EVENT_MQTT_CONNECTED,
    METEO_EVENT_TRANSMIT_COMPLETE,
    METEO_EVENT_LIGHTNING,
};

/** Creates the default event loop. Call once at boot. */
esp_err_t app_events_init(void);

#ifdef __cplusplus
}
#endif
