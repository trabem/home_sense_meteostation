#include "board.h"

#include "app_flow.h"
#include "driver/usb_serial_jtag.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pca9536.h"

static const char *TAG = "BOARD";

static pca9536_handle_t s_pca9536;

esp_err_t board_init(i2c_master_bus_handle_t i2c_bus)
{
    ESP_RETURN_ON_FALSE(i2c_bus != NULL, ESP_ERR_INVALID_ARG, TAG, "i2c bus required");

    /* CHARGE_SOURCE_STATUS gets a pull-down: a floating pin (bring-up board
     * without the TPS2121) must read "not USB" so the device keeps its
     * battery-safe deep-sleep behaviour. */
    const gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_WAKE_IRQ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const gpio_config_t charge_source_input = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_CHARGE_SOURCE_STATUS),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&charge_source_input), TAG, "gpio config failed");
    ESP_RETURN_ON_ERROR(gpio_config(&inputs), TAG, "gpio config failed");

    /* PCA9536 may be absent (bare board, bring-up): degrade instead of
     * failing init. J4 then falls back to the safe default (LoRa) and the
     * charge state reports UNKNOWN. */
    const pca9536_config_t pca_config = PCA9536_CONFIG_DEFAULT();
    if (pca9536_create(i2c_bus, &pca_config, &s_pca9536) != ESP_OK) {
        ESP_LOGW(TAG, "PCA9536 not found, board I/O degraded (J4->LoRa fallback)");
        s_pca9536 = NULL;
    }

    return ESP_OK;
}

esp_err_t board_get_transport_mode(transport_mode_t *mode)
{
    ESP_RETURN_ON_FALSE(mode != NULL, ESP_ERR_INVALID_ARG, TAG, "mode required");

#if defined(CONFIG_METEO_TRANSPORT_FORCE_WIFI)
    *mode = TRANSPORT_MODE_WIFI;
    ESP_LOGW(TAG, "transport forced to Wi-Fi (development override)");
    return ESP_OK;
#elif defined(CONFIG_METEO_TRANSPORT_FORCE_LORA)
    *mode = TRANSPORT_MODE_LORA;
    ESP_LOGW(TAG, "transport forced to LoRa (development override)");
    return ESP_OK;
#endif

    ESP_RETURN_ON_FALSE(s_pca9536 != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "board not initialized or PCA9536 missing");

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

static bool s_usj_driver_installed;

/* Fallback for boards where no power-source status line is wired: an
 * enumerated USB host on the native USB-Serial-JTAG pins means the cable
 * is plugged into a computer. */
static bool usb_serial_jtag_host_connected(void)
{
#if CONFIG_METEO_USB_DETECT_USJ
    if (!s_usj_driver_installed) {
        usb_serial_jtag_driver_config_t usj_config =
            USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        const esp_err_t err = usb_serial_jtag_driver_install(&usj_config);
        if (err == ESP_ERR_INVALID_STATE) {
            s_usj_driver_installed = true;  /* console already owns it */
        } else if (err != ESP_OK) {
            ESP_LOGD(TAG, "usb-serial-jtag unavailable: %s", esp_err_to_name(err));
            return false;
        } else {
            s_usj_driver_installed = true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));  /* let host enumeration settle */
    }
    return usb_serial_jtag_is_connected();
#else
    return false;
#endif
}

bool board_usb_power_present(void)
{
    power_source_t source = POWER_SOURCE_UNKNOWN;
    if (board_get_power_source(&source) == ESP_OK && source == POWER_SOURCE_USB) {
        return true;
    }
    return usb_serial_jtag_host_connected();
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
