#include "power_manager.h"

#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"

static const char *TAG = "POWER";

struct power_manager {
    gpio_num_t wake_irq_gpio;
    uint64_t timer_interval_us;
};

esp_err_t power_manager_create(const power_manager_config_t *config,
                               power_manager_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(config != NULL && out_handle != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "config/handle required");

    /* Fail loudly: the whole lightning wake-up feature depends on this pin. */
    ESP_RETURN_ON_FALSE(esp_sleep_is_valid_wakeup_gpio(config->wake_irq_gpio),
                        ESP_ERR_INVALID_ARG, TAG,
                        "GPIO%d is not a valid deep-sleep wake source",
                        (int)config->wake_irq_gpio);

    struct power_manager *pm = calloc(1, sizeof(*pm));
    ESP_RETURN_ON_FALSE(pm != NULL, ESP_ERR_NO_MEM, TAG, "no mem");

    pm->wake_irq_gpio = config->wake_irq_gpio;
    pm->timer_interval_us = config->timer_interval_us;
    *out_handle = pm;
    return ESP_OK;
}

app_wakeup_reason_t power_manager_get_wakeup_reason(power_manager_handle_t handle)
{
    (void)handle;

    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        return APP_WAKE_POWER_ON;
    }

    const uint32_t causes = esp_sleep_get_wakeup_causes();
    if (causes & (1u << ESP_SLEEP_WAKEUP_TIMER)) {
        return APP_WAKE_TIMER;
    }
    if (causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        return APP_WAKE_LIGHTNING_IRQ;
    }
    return APP_WAKE_UNKNOWN;
}

esp_err_t power_manager_prepare_sleep(power_manager_handle_t handle, uint64_t wake_after_us)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle required");

    const uint64_t interval = wake_after_us ? wake_after_us : handle->timer_interval_us;
    ESP_RETURN_ON_ERROR(esp_sleep_enable_timer_wakeup(interval), TAG, "timer wake failed");
    ESP_RETURN_ON_ERROR(gpio_wakeup_enable(handle->wake_irq_gpio, GPIO_INTR_HIGH_LEVEL),
                        TAG, "gpio wake config failed");
    ESP_RETURN_ON_ERROR(esp_sleep_enable_gpio_wakeup(), TAG, "gpio wake failed");

    ESP_LOGI(TAG, "wake sources armed: timer=%llu us, WAKE_IRQ=GPIO%d (HIGH)",
             (unsigned long long)interval, (int)handle->wake_irq_gpio);
    return ESP_OK;
}

void power_manager_enter_deep_sleep(void)
{
    esp_deep_sleep_start();
}
