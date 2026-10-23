#pragma once

#include <stdbool.h>

#include "telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure application decision logic.
 *
 * No hardware, no ESP-IDF dependencies: fully unit-testable on the host.
 */

typedef enum {
    APP_WAKE_POWER_ON = 0,
    APP_WAKE_TIMER,
    APP_WAKE_LIGHTNING_IRQ,
    APP_WAKE_UNKNOWN,
} app_wakeup_reason_t;

typedef enum {
    APP_ACTION_SLEEP = 0,       /* nothing to do, go back to deep sleep */
    APP_ACTION_TELEMETRY,       /* periodic measurement cycle */
    APP_ACTION_LIGHTNING,       /* process and transmit a lightning event */
} app_action_t;

/**
 * Route a wake-up to an application action.
 *
 *  - POWER_ON / TIMER / UNKNOWN -> telemetry cycle (UNKNOWN errs on the side
 *    of doing useful work rather than silently sleeping forever)
 *  - LIGHTNING_IRQ + AS3935 reported a lightning interrupt -> lightning flow
 *  - LIGHTNING_IRQ + any other AS3935 interrupt source -> sleep, no event
 */
app_action_t app_flow_decide(app_wakeup_reason_t wake, bool irq_is_lightning);

/**
 * Board-level J4 -> transport mapping. Polarity is a compile-time
 * board policy passed in explicitly, so this stays testable.
 */
transport_mode_t app_flow_transport_from_j4(bool j4_level, bool wifi_active_high);

#ifdef __cplusplus
}
#endif
