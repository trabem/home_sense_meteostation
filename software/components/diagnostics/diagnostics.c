#include "diagnostics.h"

#include <string.h>

static diagnostics_t s_diagnostics;

void diagnostics_init(uint32_t boot_count)
{
    memset(&s_diagnostics, 0, sizeof(s_diagnostics));
    s_diagnostics.boot_count = boot_count;
}

void diagnostics_increment(diag_counter_t counter)
{
    switch (counter) {
    case DIAG_WIFI_FAILURE:
        s_diagnostics.wifi_failures++;
        break;
    case DIAG_MQTT_FAILURE:
        s_diagnostics.mqtt_failures++;
        break;
    case DIAG_LORA_FAILURE:
        s_diagnostics.lora_failures++;
        break;
    case DIAG_SENSOR_FAILURE:
        s_diagnostics.sensor_failures++;
        break;
    }
}

void diagnostics_get(diagnostics_t *out)
{
    if (out != NULL) {
        *out = s_diagnostics;
    }
}
