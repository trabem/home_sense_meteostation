#include "app_flow.h"

app_action_t app_flow_decide(app_wakeup_reason_t wake, bool irq_is_lightning)
{
    switch (wake) {
    case APP_WAKE_LIGHTNING_IRQ:
        return irq_is_lightning ? APP_ACTION_LIGHTNING : APP_ACTION_SLEEP;
    case APP_WAKE_POWER_ON:
    case APP_WAKE_TIMER:
    case APP_WAKE_UNKNOWN:
    default:
        return APP_ACTION_TELEMETRY;
    }
}

transport_mode_t app_flow_transport_from_j4(bool j4_level, bool wifi_active_high)
{
    bool wifi_selected = (j4_level == wifi_active_high);
    return wifi_selected ? TRANSPORT_MODE_WIFI : TRANSPORT_MODE_LORA;
}
