#include "board.h"

#include "app_flow.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "pca9536.h"

static const char *TAG = "BOARD";

static pca9536_handle_t s_pca9536;

esp_err_t board_init(i2c_master_bus_handle_t i2c_bus)
{
    ESP_RETURN_ON_FALSE(i2c_bus != NULL, ESP_ERR_INVALID_ARG, TAG, "i2c bus required");

    const gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_WAKE_IRQ) | (1ULL << BOARD_GPIO_CHARGE_SOURCE_STATUS),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&inputs), TAG, "gpio config failed");

    const pca9536_config_t pca_config = PCA9536_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(pca9536_create(i2c_bus, &pca_config, &s_pca9536), TAG,
                        "pca9536 create failed");

    return ESP_OK;
}

esp_err_t board_get_transport_mode(transport_mode_t *mode)
{
    ESP_RETURN_ON_FALSE(mode != NULL && s_pca9536 != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "board not initialized");

    bool j4_level = false;
    ESP_RETURN_ON_ERROR(pca9536_read_pin(s_pca9536, BOARD_PCA9536_PIN_J4, &j4_level), TAG,
                        "J4 read failed");

    *mode = app_flow_transport_from_j4(j4_level, CONFIG_METEO_J4_WIFI_ACTIVE_LEVEL != 0);
    ESP_LOGI(TAG, "J4=%d -> transport=%s", (int)j4_level,
             *mode == TRANSPORT_MODE_WIFI ? "wifi" : "lora");
    return ESP_OK;
}

esp_err_t board_get_charge_state(charge_state_t *state)
{
    ESP_RETURN_ON_FALSE(state != NULL && s_pca9536 != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "board not initialized");

    uint8_t port = 0;
    ESP_RETURN_ON_ERROR(pca9536_read_port(s_pca9536, &port), TAG, "charger status read failed");

    /* TP5000 outputs are open-drain with pull-ups: LOW = active */
    const bool chrg_active = (port & (1u << BOARD_PCA9536_PIN_CHRG_STATUS)) == 0;
    const bool stdby_active = (port & (1u << BOARD_PCA9536_PIN_STDBY_STATUS)) == 0;

    if (chrg_active && stdby_active) {
        *state = CHARGE_STATE_FAULT;
    } else if (chrg_active) {
        *state = CHARGE_STATE_CHARGING;
    } else if (stdby_active) {
        *state = CHARGE_STATE_COMPLETE;
    } else {
        *state = CHARGE_STATE_IDLE;
    }
    return ESP_OK;
}

esp_err_t board_get_power_source(power_source_t *source)
{
    ESP_RETURN_ON_FALSE(source != NULL, ESP_ERR_INVALID_ARG, TAG, "source required");

    /* TPS2121 ST output: single digital line, HIGH = IN1 (USB) selected.
     * Polarity per schematic rev; only these two states are reported. */
    *source = gpio_get_level(BOARD_GPIO_CHARGE_SOURCE_STATUS) ? POWER_SOURCE_USB
                                                              : POWER_SOURCE_SOLAR;
    return ESP_OK;
}

esp_err_t board_get_device_id(char *buf, size_t buf_size)
{
    ESP_RETURN_ON_FALSE(buf != NULL && buf_size >= BOARD_DEVICE_ID_MAX_LEN, ESP_ERR_INVALID_ARG,
                        TAG, "device id buffer too small");

    uint8_t mac[6] = {0};
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), TAG, "mac read failed");

    snprintf(buf, buf_size, "meteo-%02x%02x%02x", mac[3], mac[4], mac[5]);
    return ESP_OK;
}
