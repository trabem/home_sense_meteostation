#include "ina226.h"

#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "ina226";

#define INA226_REG_CONFIG       0x00
#define INA226_REG_SHUNT_V      0x01
#define INA226_REG_BUS_V        0x02
#define INA226_REG_POWER        0x03
#define INA226_REG_CURRENT      0x04
#define INA226_REG_CALIBRATION  0x05
#define INA226_REG_MASK_ENABLE  0x06
#define INA226_REG_DIE_ID       0xFF

#define INA226_DIE_ID_EXPECTED  0x2260

/* averages x1, Vbus/Vshunt conversion 1.1 ms, mode shunt+bus triggered */
#define INA226_CONFIG_TRIGGERED  0x0123
#define INA226_CONFIG_POWER_DOWN 0x0000

#define INA226_MASK_CVRF        0x0008
#define INA226_CVRF_TIMEOUT_MS  20

#define INA226_BUS_V_LSB_V      0.00125f
#define INA226_POWER_LSB_MULT   25.0f

struct ina226 {
    i2c_master_dev_handle_t dev;
    float current_lsb_a;
    float power_lsb_w;
};

static esp_err_t ina226_write_reg16(struct ina226 *mon, uint8_t reg, uint16_t value)
{
    uint8_t buf[3] = { reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
    return i2c_master_transmit(mon->dev, buf, sizeof(buf), CONFIG_METEO_I2C_TIMEOUT_MS);
}

static esp_err_t ina226_read_reg16(struct ina226 *mon, uint8_t reg, uint16_t *value)
{
    uint8_t data[2];
    esp_err_t err = i2c_master_transmit_receive(mon->dev, &reg, 1, data, sizeof(data),
                                                CONFIG_METEO_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    *value = (uint16_t)((data[0] << 8) | data[1]);
    return ESP_OK;
}

esp_err_t ina226_create(i2c_master_bus_handle_t bus,
                        const ina226_config_t *config,
                        ina226_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(bus != NULL && config != NULL && out_handle != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(config->shunt_resistance_ohm > 0.0f &&
                        config->max_expected_current_a > 0.0f,
                        ESP_ERR_INVALID_ARG, TAG, "invalid calibration inputs");

    struct ina226 *mon = calloc(1, sizeof(*mon));
    ESP_RETURN_ON_FALSE(mon != NULL, ESP_ERR_NO_MEM, TAG, "out of memory");

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_address,
        .scl_speed_hz = CONFIG_METEO_I2C_FREQUENCY_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_config, &mon->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(err));
        free(mon);
        return err;
    }

    uint16_t die_id = 0;
    err = ina226_read_reg16(mon, INA226_REG_DIE_ID, &die_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no device at 0x%02X: %s", config->i2c_address,
                 esp_err_to_name(err));
        i2c_master_bus_rm_device(mon->dev);
        free(mon);
        return ESP_ERR_NOT_FOUND;
    }
    if (die_id != INA226_DIE_ID_EXPECTED) {
        ESP_LOGW(TAG, "0x%02X reports die ID 0x%04X (expected 0x%04X), clone?",
                 config->i2c_address, die_id, INA226_DIE_ID_EXPECTED);
    }

    mon->current_lsb_a = config->max_expected_current_a / 32768.0f;
    mon->power_lsb_w = INA226_POWER_LSB_MULT * mon->current_lsb_a;
    uint16_t cal = (uint16_t)(0.00512f / (mon->current_lsb_a * config->shunt_resistance_ohm));

    err = ina226_write_reg16(mon, INA226_REG_CALIBRATION, cal);
    if (err == ESP_OK) {
        err = ina226_write_reg16(mon, INA226_REG_CONFIG, INA226_CONFIG_POWER_DOWN);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "calibration write failed: %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(mon->dev);
        free(mon);
        return err;
    }

    *out_handle = mon;
    ESP_LOGI(TAG, "INA226 at 0x%02X ready, CAL=%u, current LSB %.3f uA",
             config->i2c_address, cal, mon->current_lsb_a * 1e6f);
    return ESP_OK;
}

esp_err_t ina226_read(ina226_handle_t handle, power_measurement_t *measurement)
{
    ESP_RETURN_ON_FALSE(handle != NULL && measurement != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid argument");

    esp_err_t err = ina226_write_reg16(handle, INA226_REG_CONFIG, INA226_CONFIG_TRIGGERED);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "trigger write failed: %s", esp_err_to_name(err));
        return err;
    }

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(INA226_CVRF_TIMEOUT_MS);
    uint16_t mask = 0;
    for (;;) {
        err = ina226_read_reg16(handle, INA226_REG_MASK_ENABLE, &mask);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "mask/enable read failed: %s", esp_err_to_name(err));
            return err;
        }
        if (mask & INA226_MASK_CVRF) {
            break;
        }
        if (xTaskGetTickCount() >= deadline) {
            ESP_LOGW(TAG, "conversion timeout");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    uint16_t bus_v_raw, power_raw;
    int16_t current_raw;
    uint16_t current_u16;
    err = ina226_read_reg16(handle, INA226_REG_BUS_V, &bus_v_raw);
    if (err == ESP_OK) {
        err = ina226_read_reg16(handle, INA226_REG_CURRENT, &current_u16);
    }
    if (err == ESP_OK) {
        err = ina226_read_reg16(handle, INA226_REG_POWER, &power_raw);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "result read failed: %s", esp_err_to_name(err));
        return err;
    }
    current_raw = (int16_t)current_u16;

    measurement->bus_voltage_v = (float)bus_v_raw * INA226_BUS_V_LSB_V;
    measurement->current_a = (float)current_raw * handle->current_lsb_a;
    measurement->power_w = (float)power_raw * handle->power_lsb_w;
    return ESP_OK;
}

esp_err_t ina226_power_down(ina226_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t err = ina226_write_reg16(handle, INA226_REG_CONFIG, INA226_CONFIG_POWER_DOWN);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "power-down write failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t ina226_destroy(ina226_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t err = i2c_master_bus_rm_device(handle->dev);
    free(handle);
    return err;
}
