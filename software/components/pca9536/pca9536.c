#include "pca9536.h"

#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "pca9536";

#define PCA9536_REG_INPUT     0x00
#define PCA9536_REG_OUTPUT    0x01
#define PCA9536_REG_POLARITY  0x02
#define PCA9536_REG_CONFIG    0x03

#define PCA9536_PIN_COUNT     4

struct pca9536 {
    i2c_master_dev_handle_t dev;
};

static esp_err_t pca9536_write_reg(struct pca9536 *expander, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(expander->dev, buf, sizeof(buf),
                               CONFIG_METEO_I2C_TIMEOUT_MS);
}

esp_err_t pca9536_create(i2c_master_bus_handle_t bus,
                         const pca9536_config_t *config,
                         pca9536_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(bus != NULL && config != NULL && out_handle != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    struct pca9536 *expander = calloc(1, sizeof(*expander));
    ESP_RETURN_ON_FALSE(expander != NULL, ESP_ERR_NO_MEM, TAG, "out of memory");

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_address,
        .scl_speed_hz = CONFIG_METEO_I2C_FREQUENCY_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_config, &expander->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(err));
        free(expander);
        return err;
    }

    err = pca9536_write_reg(expander, PCA9536_REG_CONFIG, config->direction_mask & 0x0F);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config write failed: %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(expander->dev);
        free(expander);
        return err;
    }

    *out_handle = expander;
    ESP_LOGI(TAG, "PCA9536 at 0x%02X ready, direction mask 0x%02X",
             config->i2c_address, config->direction_mask & 0x0F);
    return ESP_OK;
}

esp_err_t pca9536_read_port(pca9536_handle_t handle, uint8_t *value)
{
    ESP_RETURN_ON_FALSE(handle != NULL && value != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid argument");

    uint8_t reg = PCA9536_REG_INPUT;
    esp_err_t err = i2c_master_transmit_receive(handle->dev, &reg, 1, value, 1,
                                                CONFIG_METEO_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "port read failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t pca9536_read_pin(pca9536_handle_t handle, uint8_t pin, bool *level)
{
    ESP_RETURN_ON_FALSE(handle != NULL && level != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(pin < PCA9536_PIN_COUNT, ESP_ERR_INVALID_ARG,
                        TAG, "pin %u out of range", pin);

    uint8_t port = 0;
    esp_err_t err = pca9536_read_port(handle, &port);
    if (err != ESP_OK) {
        return err;
    }
    *level = (port >> pin) & 0x01;
    return ESP_OK;
}

esp_err_t pca9536_destroy(pca9536_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t err = i2c_master_bus_rm_device(handle->dev);
    free(handle);
    return err;
}
