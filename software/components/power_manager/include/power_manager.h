#pragma once

#include <stdint.h>

#include "app_flow.h"
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Deep-sleep orchestration (TZ 10, 30, 40).
 *
 * Maps ESP-IDF wakeup/reset causes to app_wakeup_reason_t and configures
 * both wake sources (timer + WAKE_IRQ GPIO) before every deep sleep.
 * Fails loudly if the configured WAKE_IRQ pin is not a valid deep-sleep
 * wake source on this chip/IDF version.
 */

typedef struct power_manager *power_manager_handle_t;

typedef struct {
    gpio_num_t wake_irq_gpio;        /* BOARD_GPIO_WAKE_IRQ */
    uint64_t timer_interval_us;      /* CONFIG_METEO_MEASUREMENT_INTERVAL_SEC */
} power_manager_config_t;

/**
 * Validates wake_irq_gpio with esp_sleep_is_valid_wakeup_gpio();
 * returns ESP_ERR_INVALID_ARG loudly if invalid.
 */
esp_err_t power_manager_create(const power_manager_config_t *config,
                               power_manager_handle_t *out_handle);

/** Maps esp_reset_reason()/esp_sleep_get_wakeup_cause() to app model. */
app_wakeup_reason_t power_manager_get_wakeup_reason(power_manager_handle_t handle);

/**
 * Centralized sleep preparation: enables timer wakeup (wake_after_us) and
 * WAKE_IRQ GPIO wakeup (HIGH level), then returns. The caller finishes any
 * peripheral shutdown and calls power_manager_enter_deep_sleep().
 */
esp_err_t power_manager_prepare_sleep(power_manager_handle_t handle,
                                      uint64_t wake_after_us);

/** esp_deep_sleep_start(); does not return. */
void power_manager_enter_deep_sleep(void);

#ifdef __cplusplus
}
#endif
