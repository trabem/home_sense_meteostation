#include "i2c_bus.h"

#include <inttypes.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "i2c_bus";

static i2c_master_bus_handle_t s_bus;

esp_err_t i2c_bus_init(const i2c_bus_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_FALSE(s_bus == NULL, ESP_ERR_INVALID_STATE, TAG, "bus already initialized");

    i2c_master_bus_config_t bus_config = {
        .i2c_port = -1,
        .sda_io_num = config->sda_gpio,
        .scl_io_num = config->scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C bus up: sda=%d scl=%d freq=%" PRIu32 " Hz",
             config->sda_gpio, config->scl_gpio, config->frequency_hz);
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get_handle(void)
{
    return s_bus;
}

esp_err_t i2c_bus_deinit(void)
{
    ESP_RETURN_ON_FALSE(s_bus != NULL, ESP_ERR_INVALID_STATE, TAG, "bus not initialized");

    esp_err_t err = i2c_del_master_bus(s_bus);
    if (err == ESP_OK) {
        s_bus = NULL;
    }
    return err;
}
