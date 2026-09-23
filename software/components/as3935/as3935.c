#include "as3935.h"

#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "as3935";

#define AS3935_REG_AFE_NOISE    0x00
#define AS3935_REG_THRESHOLD    0x01
#define AS3935_REG_LIGHTNING    0x02
#define AS3935_REG_INT          0x03
#define AS3935_REG_ENERGY_LSB   0x04
#define AS3935_REG_ENERGY_MSB   0x05
#define AS3935_REG_ENERGY_MM    0x06
#define AS3935_REG_DISTANCE     0x07
#define AS3935_REG_TRCO         0x08
#define AS3935_REG_DIRECT_CMD   0x3C

#define AS3935_AFE_GAIN_INDOOR   0x12   /* AFE_G bits[6:1] */
#define AS3935_AFE_GAIN_OUTDOOR  0x0E

#define AS3935_CL_STAT           0x80   /* REG0x02 bit7 per datasheet */
#define AS3935_SREJ_DEFAULT      0x02
#define AS3935_WATCHDOG_DEFAULT  0x02

#define AS3935_CMD_CALIB_RCO     0x96

#define AS3935_DISP_TRCO         0x80   /* REG0x08 bit7 */
#define AS3935_TRCO_DONE         0x40   /* REG0x08 bit6 */
#define AS3935_TRCO_FAIL         0x20   /* REG0x08 bit5 */
#define AS3935_TRCO_TIMEOUT_MS   20
#define AS3935_CALIB_DELAY_MS    5

struct as3935 {
    i2c_master_dev_handle_t dev;
};

static esp_err_t as3935_write_reg(struct as3935 *sensor, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(sensor->dev, buf, sizeof(buf),
                               CONFIG_METEO_I2C_TIMEOUT_MS);
}

static esp_err_t as3935_read_reg(struct as3935 *sensor, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(sensor->dev, &reg, 1, value, 1,
                                       CONFIG_METEO_I2C_TIMEOUT_MS);
}

static esp_err_t as3935_modify_reg(struct as3935 *sensor, uint8_t reg,
                                   uint8_t set_mask, uint8_t clear_mask)
{
    uint8_t value;
    esp_err_t err = as3935_read_reg(sensor, reg, &value);
    if (err != ESP_OK) {
        return err;
    }
    value = (value | set_mask) & (uint8_t)~clear_mask;
    return as3935_write_reg(sensor, reg, value);
}

static void as3935_calibrate_rco(struct as3935 *sensor)
{
    esp_err_t err = as3935_write_reg(sensor, AS3935_REG_DIRECT_CMD, AS3935_CMD_CALIB_RCO);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "calib_RCO command failed: %s", esp_err_to_name(err));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(AS3935_CALIB_DELAY_MS));

    err = as3935_write_reg(sensor, AS3935_REG_TRCO, AS3935_DISP_TRCO);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TRCO display on failed: %s", esp_err_to_name(err));
        return;
    }

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(AS3935_TRCO_TIMEOUT_MS);
    uint8_t trco = 0;
    for (;;) {
        err = as3935_read_reg(sensor, AS3935_REG_TRCO, &trco);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "TRCO read failed: %s", esp_err_to_name(err));
            break;
        }
        if (trco & AS3935_TRCO_DONE) {
            if (trco & AS3935_TRCO_FAIL) {
                ESP_LOGW(TAG, "TRCO calibration unsuccessful, sensor still usable");
            }
            break;
        }
        if (xTaskGetTickCount() >= deadline) {
            ESP_LOGW(TAG, "TRCO calibration timeout, sensor still usable");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (as3935_write_reg(sensor, AS3935_REG_TRCO, 0x00) != ESP_OK) {
        ESP_LOGW(TAG, "TRCO display off failed");
    }
}

esp_err_t as3935_create(i2c_master_bus_handle_t bus,
                        const as3935_config_t *config,
                        as3935_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(bus != NULL && config != NULL && out_handle != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(config->noise_floor <= 7, ESP_ERR_INVALID_ARG,
                        TAG, "noise_floor out of range");

    struct as3935 *sensor = calloc(1, sizeof(*sensor));
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_NO_MEM, TAG, "out of memory");

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_address,
        .scl_speed_hz = CONFIG_METEO_I2C_FREQUENCY_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_config, &sensor->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(err));
        free(sensor);
        return err;
    }

    uint8_t gain = config->indoor ? AS3935_AFE_GAIN_INDOOR : AS3935_AFE_GAIN_OUTDOOR;
    err = as3935_write_reg(sensor, AS3935_REG_AFE_NOISE, gain << 1);
    if (err == ESP_OK) {
        err = as3935_write_reg(sensor, AS3935_REG_THRESHOLD,
                               (uint8_t)((config->noise_floor << 4) | AS3935_WATCHDOG_DEFAULT));
    }
    if (err == ESP_OK) {
        err = as3935_write_reg(sensor, AS3935_REG_LIGHTNING, AS3935_SREJ_DEFAULT);
    }
    if (err == ESP_OK) {
        /* MASK_DIST (bit5) clear: disturbers still raise IRQ */
        err = as3935_write_reg(sensor, AS3935_REG_INT, 0x00);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no response at 0x%02X: %s", config->i2c_address,
                 esp_err_to_name(err));
        i2c_master_bus_rm_device(sensor->dev);
        free(sensor);
        return ESP_ERR_NOT_FOUND;
    }

    as3935_calibrate_rco(sensor);
    as3935_clear_statistics(sensor);

    *out_handle = sensor;
    ESP_LOGI(TAG, "AS3935 at 0x%02X ready (%s AFE)", config->i2c_address,
             config->indoor ? "indoor" : "outdoor");
    return ESP_OK;
}

esp_err_t as3935_get_interrupt_reason(as3935_handle_t handle,
                                      as3935_irq_reason_t *reason)
{
    ESP_RETURN_ON_FALSE(handle != NULL && reason != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid argument");

    uint8_t value;
    esp_err_t err = as3935_read_reg(handle, AS3935_REG_INT, &value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "INT read failed: %s", esp_err_to_name(err));
        return err;
    }
    *reason = (as3935_irq_reason_t)(value & 0x0F);
    return ESP_OK;
}

esp_err_t as3935_read_lightning_distance(as3935_handle_t handle,
                                         uint8_t *distance_km)
{
    ESP_RETURN_ON_FALSE(handle != NULL && distance_km != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid argument");

    uint8_t value;
    esp_err_t err = as3935_read_reg(handle, AS3935_REG_DISTANCE, &value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "distance read failed: %s", esp_err_to_name(err));
        return err;
    }
    *distance_km = value & 0x3F;
    return ESP_OK;
}

esp_err_t as3935_read_lightning_energy(as3935_handle_t handle, uint32_t *energy)
{
    ESP_RETURN_ON_FALSE(handle != NULL && energy != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid argument");

    uint8_t reg = AS3935_REG_ENERGY_LSB;
    uint8_t data[3];
    esp_err_t err = i2c_master_transmit_receive(handle->dev, &reg, 1, data, sizeof(data),
                                                CONFIG_METEO_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "energy read failed: %s", esp_err_to_name(err));
        return err;
    }
    *energy = ((uint32_t)(data[2] & 0x1F) << 16) | ((uint32_t)data[1] << 8) | data[0];
    return ESP_OK;
}

esp_err_t as3935_clear_statistics(as3935_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t err = as3935_modify_reg(handle, AS3935_REG_LIGHTNING, AS3935_CL_STAT, 0);
    if (err == ESP_OK) {
        err = as3935_modify_reg(handle, AS3935_REG_LIGHTNING, 0, AS3935_CL_STAT);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "clear statistics failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t as3935_destroy(as3935_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t err = i2c_master_bus_rm_device(handle->dev);
    free(handle);
    return err;
}
