#include "sensor_manager.h"

#include <stdlib.h>

#include "board.h"
#include "diagnostics.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "SENSORS";

struct sensor_manager {
    bme280_handle_t bme280;
    ina226_handle_t battery_ina;
    ina226_handle_t solar_ina;
};

esp_err_t sensor_manager_init(sensor_manager_handle_t *out_handle,
                              const sensor_manager_config_t *config)
{
    ESP_RETURN_ON_FALSE(out_handle != NULL && config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "handle/config required");

    struct sensor_manager *sm = calloc(1, sizeof(*sm));
    ESP_RETURN_ON_FALSE(sm != NULL, ESP_ERR_NO_MEM, TAG, "no mem");

    sm->bme280 = config->bme280;
    sm->battery_ina = config->battery_ina;
    sm->solar_ina = config->solar_ina;
    *out_handle = sm;
    return ESP_OK;
}

static void measure_environment(sensor_manager_handle_t sm, telemetry_t *telemetry)
{
    if (sm->bme280 == NULL) {
        return;
    }
    if (bme280_read_forced(sm->bme280, &telemetry->environment) == ESP_OK) {
        telemetry->environment_valid = true;
    } else {
        ESP_LOGW(TAG, "BME280 measurement failed");
        diagnostics_increment(DIAG_SENSOR_FAILURE);
    }
}

static void measure_power(const char *name, ina226_handle_t ina,
                          power_measurement_t *out, bool *valid)
{
    if (ina == NULL) {
        return;
    }
    if (ina226_read(ina, out) == ESP_OK) {
        *valid = true;
    } else {
        ESP_LOGW(TAG, "%s INA226 measurement failed", name);
        diagnostics_increment(DIAG_SENSOR_FAILURE);
    }
    if (ina226_power_down(ina) != ESP_OK) {
        ESP_LOGW(TAG, "%s INA226 power-down failed", name);
    }
}

esp_err_t sensor_manager_measure(sensor_manager_handle_t handle, telemetry_t *telemetry)
{
    ESP_RETURN_ON_FALSE(handle != NULL && telemetry != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "handle/telemetry required");

    measure_environment(handle, telemetry);
    measure_power("battery", handle->battery_ina, &telemetry->battery, &telemetry->battery_valid);
    measure_power("solar", handle->solar_ina, &telemetry->solar, &telemetry->solar_valid);

    charge_state_t charge = CHARGE_STATE_UNKNOWN;
    if (board_get_charge_state(&charge) == ESP_OK) {
        telemetry->charging = (charge == CHARGE_STATE_CHARGING);
        telemetry->charge_complete = (charge == CHARGE_STATE_COMPLETE);
    } else {
        ESP_LOGW(TAG, "charge state read failed");
        diagnostics_increment(DIAG_SENSOR_FAILURE);
    }

    const bool any = telemetry->environment_valid || telemetry->battery_valid
                     || telemetry->solar_valid;
    return any ? ESP_OK : ESP_FAIL;
}

esp_err_t sensor_manager_deinit(sensor_manager_handle_t handle)
{
    free(handle);
    return ESP_OK;
}
