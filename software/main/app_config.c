#include "app_config.h"

#include "board.h"
#include "driver/spi_master.h"
#include "esp_app_desc.h"
#include "sdkconfig.h"

void app_config_fill_radio(sx1262_config_t *config)
{
#if defined(CONFIG_METEO_LORA_BANDWIDTH_125)
    const uint32_t bandwidth_hz = 125000;
#elif defined(CONFIG_METEO_LORA_BANDWIDTH_250)
    const uint32_t bandwidth_hz = 250000;
#else
    const uint32_t bandwidth_hz = 500000;
#endif

#if defined(CONFIG_METEO_LORA_CODING_RATE_4_6)
    const uint8_t coding_rate = 2;
#elif defined(CONFIG_METEO_LORA_CODING_RATE_4_7)
    const uint8_t coding_rate = 3;
#elif defined(CONFIG_METEO_LORA_CODING_RATE_4_8)
    const uint8_t coding_rate = 4;
#else
    const uint8_t coding_rate = 1;
#endif

    *config = (sx1262_config_t){
        .spi_host = SPI2_HOST,
        .mosi_gpio = BOARD_GPIO_SPI_MOSI,
        .miso_gpio = BOARD_GPIO_SPI_MISO,
        .sck_gpio = BOARD_GPIO_SPI_SCK,
        .nss_gpio = BOARD_GPIO_LORA_NSS,
        .busy_gpio = BOARD_GPIO_LORA_BUSY,
        .nrst_gpio = BOARD_GPIO_LORA_NRST,
        .dio1_gpio = BOARD_GPIO_LORA_DIO1,
        .frequency_hz = CONFIG_METEO_LORA_FREQUENCY_HZ,
        .tx_power_dbm = CONFIG_METEO_LORA_TX_POWER_DBM,
        .spreading_factor = CONFIG_METEO_LORA_SPREADING_FACTOR,
        .bandwidth_hz = bandwidth_hz,
        .coding_rate = coding_rate,
        .tx_timeout_ms = CONFIG_METEO_LORA_TX_TIMEOUT_MS,
        .busy_timeout_ms = CONFIG_METEO_LORA_BUSY_TIMEOUT_MS,
    };
}

void app_config_fill_mqtt(mqtt_transport_config_t *config, const char *device_id)
{
    *config = (mqtt_transport_config_t){
        .broker_uri = CONFIG_METEO_MQTT_URI,
        .username = CONFIG_METEO_MQTT_USERNAME,
        .password = CONFIG_METEO_MQTT_PASSWORD,
        .device_id = device_id,
        .root_topic = CONFIG_METEO_MQTT_ROOT_TOPIC,
        .qos = CONFIG_METEO_MQTT_QOS,
        .connect_timeout_ms = CONFIG_METEO_MQTT_CONNECT_TIMEOUT_MS,
        .publish_timeout_ms = CONFIG_METEO_MQTT_PUBLISH_TIMEOUT_MS,
        .tls_skip_verify = false,
        .ca_cert_pem = NULL,
    };
}

void app_config_fill_power(power_manager_config_t *config)
{
    *config = (power_manager_config_t){
        .wake_irq_gpio = BOARD_GPIO_WAKE_IRQ,
        .timer_interval_us = (uint64_t)CONFIG_METEO_MEASUREMENT_INTERVAL_SEC * 1000000ULL,
    };
}

const char *app_config_fw_version(void)
{
    return esp_app_get_description()->version;
}
