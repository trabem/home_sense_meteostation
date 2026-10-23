#include "app.h"

#include <string.h>
#include <time.h>

#include "app_config.h"
#include "app_events.h"
#include "app_flow.h"
#include "app_storage.h"
#include "as3935.h"
#include "bme280.h"
#include "board.h"
#include "diagnostics.h"
#include "driver/gpio.h"
#include "ds3231.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "ina226.h"
#include "lightning_service.h"
#include "power_manager.h"
#include "sdkconfig.h"
#include "sensor_manager.h"
#include "telemetry_codec.h"
#include "transport_factory.h"
#include "wifi_manager.h"
#include "wifi_provisioning.h"

static const char *TAG = "APP";

/* RTC readings below this are treated as invalid (battery lost / never set). */
#define RTC_MIN_VALID_EPOCH  1704067200LL  /* 2024-01-01 */
#define SNTP_SYNC_TIMEOUT_MS 10000

typedef struct {
    message_type_t type;
    const uint8_t *payload;
    size_t length;
} outgoing_message_t;

typedef struct {
    char device_id[BOARD_DEVICE_ID_MAX_LEN];
    const char *fw_version;
    uint32_t device_hash;
    uint32_t boot_count;
    app_wakeup_reason_t wake_reason;
    transport_mode_t transport_mode;
    power_manager_handle_t power;
    ds3231_handle_t rtc;
    as3935_handle_t as3935;
    sx1262_handle_t radio;
    bool radio_asleep;
    bool continuous;            /* USB-powered: timer loop instead of deep sleep */
    TaskHandle_t main_task;     /* runtime WAKE_IRQ notifications */
} app_context_t;

static app_context_t s_app;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static int64_t rtc_now(void)
{
    int64_t now = 0;
    if (s_app.rtc == NULL || ds3231_get_unix_time(s_app.rtc, &now) != ESP_OK) {
        ESP_LOGW(TAG, "RTC read failed, timestamping with 0");
        diagnostics_increment(DIAG_SENSOR_FAILURE);
        return 0;
    }
    return now;
}

static void set_log_level_from_config(void)
{
#if defined(CONFIG_METEO_LOG_LEVEL_ERROR)
    esp_log_level_set("*", ESP_LOG_ERROR);
#elif defined(CONFIG_METEO_LOG_LEVEL_WARN)
    esp_log_level_set("*", ESP_LOG_WARN);
#elif defined(CONFIG_METEO_LOG_LEVEL_DEBUG)
    esp_log_level_set("*", ESP_LOG_DEBUG);
#else
    esp_log_level_set("*", ESP_LOG_INFO);
#endif
}

static esp_err_t ensure_radio_created(void)
{
    if (s_app.radio != NULL) {
        return ESP_OK;
    }
    sx1262_config_t radio_config;
    app_config_fill_radio(&radio_config);
    ESP_RETURN_ON_ERROR(sx1262_create(&radio_config, &s_app.radio), TAG,
                        "sx1262 create failed");
    s_app.radio_asleep = false;
    return ESP_OK;
}

/* Wi-Fi mode must never leave the SX1262 burning current in standby. */
static void ensure_radio_asleep(void)
{
    if (s_app.radio == NULL || s_app.radio_asleep) {
        return;
    }
    if (sx1262_sleep(s_app.radio) == ESP_OK) {
        s_app.radio_asleep = true;
    } else {
        ESP_LOGW(TAG, "SX1262 sleep command failed");
    }
}

/* ------------------------------------------------------------------ */
/* SNTP -> DS3231 sync, only when the RTC time is clearly invalid      */
/* ------------------------------------------------------------------ */

static void maybe_sync_rtc(void)
{
    if (rtc_now() >= RTC_MIN_VALID_EPOCH) {
        return;  /* RTC looks sane; do not resync on every wake */
    }

    ESP_LOGI(TAG, "RTC time invalid, syncing via SNTP");
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_METEO_SNTP_SERVER);
    if (esp_netif_sntp_init(&sntp_config) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP init failed");
        return;
    }
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(SNTP_SYNC_TIMEOUT_MS)) == ESP_OK) {
        const time_t now = time(NULL);
        if (now >= RTC_MIN_VALID_EPOCH && s_app.rtc != NULL) {
            if (ds3231_set_unix_time(s_app.rtc, (int64_t)now) == ESP_OK) {
                ESP_LOGI(TAG, "DS3231 synced");
            }
        }
    } else {
        ESP_LOGW(TAG, "SNTP sync timed out");
    }
    esp_netif_sntp_deinit();
}

/* ------------------------------------------------------------------ */
/* Transmission                                                        */
/* ------------------------------------------------------------------ */

static esp_err_t transmit_via_wifi(const outgoing_message_t *messages, size_t count)
{
    ensure_radio_asleep();

    ESP_RETURN_ON_ERROR(wifi_manager_init(), TAG, "wifi init failed");

    esp_err_t err = ESP_OK;
    bool has_credentials = false;
    if (wifi_manager_has_credentials(&has_credentials) != ESP_OK) {
        has_credentials = false;
    }

    if (!has_credentials) {
        ESP_LOGI(TAG, "no Wi-Fi credentials, starting BLE provisioning");
        err = wifi_provisioning_run(CONFIG_METEO_PROV_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "provisioning failed: %s", esp_err_to_name(err));
            diagnostics_increment(DIAG_WIFI_FAILURE);
            wifi_manager_stop();
            return err;
        }
    }

    err = wifi_manager_connect(CONFIG_METEO_WIFI_CONNECT_TIMEOUT_MS,
                               CONFIG_METEO_WIFI_MAX_RETRIES);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi connect failed, going back to sleep");
        diagnostics_increment(DIAG_WIFI_FAILURE);
        wifi_manager_stop();
        return err;
    }

    maybe_sync_rtc();

    mqtt_transport_config_t mqtt_config;
    app_config_fill_mqtt(&mqtt_config, s_app.device_id);
    const transport_factory_config_t factory_config = { .mqtt = mqtt_config };

    transport_t *transport = NULL;
    err = transport_create(TRANSPORT_MODE_WIFI, &factory_config, &transport);
    if (err == ESP_OK) {
        err = transport_open(transport);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT connect failed");
        diagnostics_increment(DIAG_MQTT_FAILURE);
    } else {
        for (size_t i = 0; i < count; i++) {
            err = transport_send(transport, messages[i].type, messages[i].payload,
                                 messages[i].length);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "MQTT publish failed: %s", esp_err_to_name(err));
                diagnostics_increment(DIAG_MQTT_FAILURE);
                break;
            }
        }
        transport_close(transport);
    }
    transport_destroy(transport);
    wifi_manager_stop();

    if (err == ESP_OK) {
        esp_event_post(METEO_EVENT, METEO_EVENT_TRANSMIT_COMPLETE, NULL, 0, 0);
    }
    return err;
}

static esp_err_t transmit_via_lora(const outgoing_message_t *messages, size_t count)
{
    ESP_RETURN_ON_ERROR(ensure_radio_created(), TAG, "radio unavailable");

    const transport_factory_config_t factory_config = {
        .lora = { .radio = s_app.radio },
    };

    transport_t *transport = NULL;
    esp_err_t err = transport_create(TRANSPORT_MODE_LORA, &factory_config, &transport);
    if (err == ESP_OK) {
        err = transport_open(transport);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LoRa radio init failed");
        diagnostics_increment(DIAG_LORA_FAILURE);
    } else {
        for (size_t i = 0; i < count; i++) {
            err = transport_send(transport, messages[i].type, messages[i].payload,
                                 messages[i].length);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "LoRa TX failed: %s", esp_err_to_name(err));
                diagnostics_increment(DIAG_LORA_FAILURE);
                break;
            }
        }
        if (transport_close(transport) == ESP_OK) {
            s_app.radio_asleep = true;
        }
    }
    transport_destroy(transport);

    if (err == ESP_OK) {
        esp_event_post(METEO_EVENT, METEO_EVENT_TRANSMIT_COMPLETE, NULL, 0, 0);
    }
    return err;
}

static esp_err_t transmit_messages(const outgoing_message_t *messages, size_t count)
{
    if (s_app.transport_mode == TRANSPORT_MODE_WIFI) {
        return transmit_via_wifi(messages, count);
    }
    return transmit_via_lora(messages, count);
}

/* ------------------------------------------------------------------ */
/* Flows                                                               */
/* ------------------------------------------------------------------ */

static void run_lightning_flow(const lightning_event_t *event)
{
    if (board_get_transport_mode(&s_app.transport_mode) != ESP_OK) {
        ESP_LOGW(TAG, "transport mode read failed, defaulting to LoRa");
        s_app.transport_mode = TRANSPORT_MODE_LORA;
    }

    static char json_buf[TELEMETRY_CODEC_JSON_BUF_SIZE];
    static uint8_t bin_buf[TELEMETRY_LORA_LIGHTNING_MAX];
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    if (s_app.transport_mode == TRANSPORT_MODE_WIFI) {
        if (lightning_codec_encode_mqtt(event, s_app.device_id, json_buf, sizeof(json_buf),
                                        &payload_len) == ESP_OK) {
            payload = (const uint8_t *)json_buf;
        }
    } else {
        if (lightning_codec_encode_lora(event, s_app.device_hash, bin_buf, sizeof(bin_buf),
                                        &payload_len) == ESP_OK) {
            payload = bin_buf;
        }
    }

    if (payload == NULL) {
        ESP_LOGE(TAG, "lightning encode failed");
        return;
    }

    const outgoing_message_t message = {
        .type = MESSAGE_TYPE_LIGHTNING, .payload = payload, .length = payload_len,
    };
    if (transmit_messages(&message, 1) == ESP_OK) {
        esp_event_post(METEO_EVENT, METEO_EVENT_LIGHTNING, NULL, 0, 0);
    }
}

static void run_telemetry_flow(void)
{
    if (board_get_transport_mode(&s_app.transport_mode) != ESP_OK) {
        ESP_LOGW(TAG, "transport mode read failed, defaulting to LoRa");
        s_app.transport_mode = TRANSPORT_MODE_LORA;
    }

    /* Measurement-level init: created only for a periodic cycle. */
    bme280_handle_t bme = NULL;
    ina226_handle_t battery_ina = NULL;
    ina226_handle_t solar_ina = NULL;

    const bme280_config_t bme_config = BME280_CONFIG_DEFAULT();
    if (bme280_create(i2c_bus_get_handle(), &bme_config, &bme) != ESP_OK) {
        ESP_LOGW(TAG, "BME280 not available");
        bme = NULL;
    }
    const ina226_config_t battery_config = {
        .i2c_address = BOARD_INA226_BATTERY_ADDRESS,
        .shunt_resistance_ohm = BOARD_INA226_BATTERY_SHUNT_OHM,
        .max_expected_current_a = 2.0f,
    };
    if (ina226_create(i2c_bus_get_handle(), &battery_config, &battery_ina) != ESP_OK) {
        ESP_LOGW(TAG, "battery INA226 not available");
        battery_ina = NULL;
    }
    const ina226_config_t solar_config = {
        .i2c_address = BOARD_INA226_SOLAR_ADDRESS,
        .shunt_resistance_ohm = BOARD_INA226_SOLAR_SHUNT_OHM,
        .max_expected_current_a = 1.0f,
    };
    if (ina226_create(i2c_bus_get_handle(), &solar_config, &solar_ina) != ESP_OK) {
        ESP_LOGW(TAG, "solar INA226 not available");
        solar_ina = NULL;
    }

    telemetry_t telemetry = {0};
    sensor_manager_handle_t sensors = NULL;
    const sensor_manager_config_t sensor_config = {
        .bme280 = bme, .battery_ina = battery_ina, .solar_ina = solar_ina,
    };
    if (sensor_manager_init(&sensors, &sensor_config) == ESP_OK) {
        sensor_manager_measure(sensors, &telemetry);
        sensor_manager_deinit(sensors);
    }

    telemetry.timestamp = rtc_now();
    telemetry.transport = s_app.transport_mode;
    telemetry.boot_count = s_app.boot_count;
    telemetry.reset_reason = (uint8_t)esp_reset_reason();
    telemetry.wake_reason = (uint8_t)s_app.wake_reason;

    /* Serialize per transport before opening any radio/network session. */
    static char json_buf[TELEMETRY_CODEC_JSON_BUF_SIZE];
    static char boot_json_buf[TELEMETRY_CODEC_JSON_BUF_SIZE];
    static uint8_t bin_buf[TELEMETRY_LORA_TELEMETRY_MAX];
    static uint8_t boot_bin_buf[TELEMETRY_LORA_BOOT_MAX];

    outgoing_message_t messages[2];
    size_t message_count = 0;

    const boot_info_t boot_info = {
        .timestamp = telemetry.timestamp,
        .boot_count = s_app.boot_count,
        .reset_reason = telemetry.reset_reason,
        .wake_reason = telemetry.wake_reason,
    };
    const bool send_boot = (s_app.wake_reason == APP_WAKE_POWER_ON);

    if (s_app.transport_mode == TRANSPORT_MODE_WIFI) {
        size_t len = 0;
        if (send_boot
            && boot_codec_encode_mqtt(&boot_info, s_app.device_id, s_app.fw_version,
                                      boot_json_buf, sizeof(boot_json_buf), &len) == ESP_OK) {
            messages[message_count++] = (outgoing_message_t){
                MESSAGE_TYPE_BOOT, (const uint8_t *)boot_json_buf, len };
        }
        if (telemetry_codec_encode_mqtt(&telemetry, s_app.device_id, s_app.fw_version,
                                        json_buf, sizeof(json_buf), &len) == ESP_OK) {
            messages[message_count++] = (outgoing_message_t){
                MESSAGE_TYPE_TELEMETRY, (const uint8_t *)json_buf, len };
        }
    } else {
        size_t len = 0;
        if (send_boot
            && boot_codec_encode_lora(&boot_info, s_app.device_hash, boot_bin_buf,
                                      sizeof(boot_bin_buf), &len) == ESP_OK) {
            messages[message_count++] = (outgoing_message_t){
                MESSAGE_TYPE_BOOT, boot_bin_buf, len };
        }
        if (telemetry_codec_encode_lora(&telemetry, s_app.device_hash, bin_buf,
                                        sizeof(bin_buf), &len) == ESP_OK) {
            messages[message_count++] = (outgoing_message_t){
                MESSAGE_TYPE_TELEMETRY, bin_buf, len };
        }
    }

    if (message_count > 0) {
        transmit_messages(messages, message_count);
    } else {
        ESP_LOGE(TAG, "telemetry encode failed");
    }

    if (bme != NULL) {
        bme280_sleep(bme);
        bme280_destroy(bme);
    }
    if (battery_ina != NULL) {
        ina226_power_down(battery_ina);
        ina226_destroy(battery_ina);
    }
    if (solar_ina != NULL) {
        ina226_power_down(solar_ina);
        ina226_destroy(solar_ina);
    }
}

/* ------------------------------------------------------------------ */
/* Lightning IRQ handling (shared by deep-sleep wake and runtime ISR)  */
/* ------------------------------------------------------------------ */

static bool handle_lightning_irq(void)
{
    if (s_app.as3935 == NULL) {
        ESP_LOGW(TAG, "WAKE_IRQ but AS3935 unavailable");
        return false;
    }

    bool is_lightning = false;
    lightning_event_t lightning = {0};
    lightning.timestamp = rtc_now();

    lightning_service_handle_t lightning_service = NULL;
    const lightning_service_config_t service_config = { .as3935 = s_app.as3935 };
    if (lightning_service_init(&lightning_service, &service_config) == ESP_OK) {
        if (lightning_service_process_irq(lightning_service, &lightning,
                                          &is_lightning) != ESP_OK) {
            ESP_LOGW(TAG, "AS3935 processing failed");
            diagnostics_increment(DIAG_SENSOR_FAILURE);
            is_lightning = false;
        }
        lightning_service_deinit(lightning_service);
    }

    if (is_lightning) {
        run_lightning_flow(&lightning);
    }
    return is_lightning;
}

/* ------------------------------------------------------------------ */
/* Continuous mode (USB-powered): timer cycles, no deep sleep          */
/* ------------------------------------------------------------------ */

static void IRAM_ATTR wake_irq_handler(void *arg)
{
    (void)arg;
    BaseType_t higher_priority_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_app.main_task, &higher_priority_woken);
    portYIELD_FROM_ISR(higher_priority_woken);
}

static void arm_wake_irq_runtime(void)
{
    if (s_app.as3935 == NULL) {
        return;
    }
    const esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "isr service install failed: %s", esp_err_to_name(err));
        return;
    }
    if (gpio_set_intr_type(BOARD_GPIO_WAKE_IRQ, GPIO_INTR_POSEDGE) != ESP_OK
        || gpio_isr_handler_add(BOARD_GPIO_WAKE_IRQ, wake_irq_handler, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "WAKE_IRQ runtime handler setup failed");
        return;
    }
    gpio_intr_enable(BOARD_GPIO_WAKE_IRQ);
}

static void continuous_loop(void)
{
    s_app.main_task = xTaskGetCurrentTaskHandle();
    arm_wake_irq_runtime();

    const TickType_t period =
        pdMS_TO_TICKS((uint64_t)CONFIG_METEO_MEASUREMENT_INTERVAL_SEC * 1000ULL);
    TickType_t next_telemetry = xTaskGetTickCount();

    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        if ((int32_t)(next_telemetry - now) <= 0) {
            run_telemetry_flow();
            next_telemetry = xTaskGetTickCount() + period;
            continue;
        }
        /* Sleep on the notification until the next measurement is due; a
         * runtime AS3935 IRQ wakes the loop immediately. */
        if (ulTaskNotifyTake(pdTRUE, next_telemetry - now) > 0) {
            ESP_LOGI(TAG, "runtime WAKE_IRQ");
            handle_lightning_irq();
        }
    }
}

/* ------------------------------------------------------------------ */
/* Boot / lifecycle                                                   */
/* ------------------------------------------------------------------ */

esp_err_t app_init(void)
{
    set_log_level_from_config();

    ESP_RETURN_ON_ERROR(app_events_init(), TAG, "events init failed");
    ESP_RETURN_ON_ERROR(app_storage_init(), TAG, "storage init failed");

    uint32_t boot_count = 0;
    if (app_storage_next_boot_count(&boot_count) != ESP_OK) {
        ESP_LOGW(TAG, "boot counter unavailable");
    }
    s_app.boot_count = boot_count;
    diagnostics_init(boot_count);

    const i2c_bus_config_t i2c_config = {
        .sda_gpio = BOARD_GPIO_I2C_SDA,
        .scl_gpio = BOARD_GPIO_I2C_SCL,
        .frequency_hz = CONFIG_METEO_I2C_FREQUENCY_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_bus_init(&i2c_config), TAG, "i2c init failed");
    ESP_RETURN_ON_ERROR(board_init(i2c_bus_get_handle()), TAG, "board init failed");
    ESP_RETURN_ON_ERROR(board_get_device_id(s_app.device_id, sizeof(s_app.device_id)), TAG,
                        "device id failed");
    s_app.fw_version = app_config_fw_version();
    s_app.device_hash = telemetry_codec_device_hash(s_app.device_id);

    const ds3231_config_t rtc_config = DS3231_CONFIG_DEFAULT();
    if (ds3231_create(i2c_bus_get_handle(), &rtc_config, &s_app.rtc) != ESP_OK) {
        ESP_LOGW(TAG, "DS3231 not available");
        s_app.rtc = NULL;
    }

    const as3935_config_t as3935_config = AS3935_CONFIG_DEFAULT();
    if (as3935_create(i2c_bus_get_handle(), &as3935_config, &s_app.as3935) != ESP_OK) {
        ESP_LOGW(TAG, "AS3935 not available, lightning detection degraded");
        s_app.as3935 = NULL;
    }

    power_manager_config_t power_config;
    app_config_fill_power(&power_config);
    /* Fails loudly if WAKE_IRQ is not a valid deep-sleep wake source. */
    ESP_RETURN_ON_ERROR(power_manager_create(&power_config, &s_app.power), TAG,
                        "power manager init failed");

    s_app.wake_reason = power_manager_get_wakeup_reason(s_app.power);

    const bool usb_power = board_usb_power_present();
#if CONFIG_METEO_FORCE_CONTINUOUS
    s_app.continuous = true;
    ESP_LOGW(TAG, "DEV: continuous mode forced, deep sleep disabled");
#elif CONFIG_METEO_USB_CONTINUOUS_MODE
    s_app.continuous = usb_power;
#else
    s_app.continuous = false;
#endif

    ESP_LOGI(TAG, "meteostation fw=%s idf=%s device=%s boot=%lu", s_app.fw_version,
             esp_get_idf_version(), s_app.device_id, (unsigned long)s_app.boot_count);
    ESP_LOGI(TAG, "reset_reason=%d wake_reason=%d usb_power=%d run_mode=%s",
             (int)esp_reset_reason(), (int)s_app.wake_reason, (int)usb_power,
             s_app.continuous ? "continuous" : "deep-sleep");
    return ESP_OK;
}

void app_run(void)
{
    if (s_app.continuous) {
        ESP_LOGI(TAG, "USB-powered: continuous mode, sleeping between cycles only");
        continuous_loop();  /* never returns */
    }

    bool is_lightning = false;

    /* Lightning wake: read the AS3935 reason before anything else. */
    if (s_app.wake_reason == APP_WAKE_LIGHTNING_IRQ) {
        is_lightning = handle_lightning_irq();
    }

    const app_action_t action = app_flow_decide(s_app.wake_reason, is_lightning);
    switch (action) {
    case APP_ACTION_LIGHTNING:
        break;  /* already processed and transmitted above */
    case APP_ACTION_TELEMETRY:
        ESP_LOGI(TAG, "flow: periodic telemetry");
        run_telemetry_flow();
        break;
    case APP_ACTION_SLEEP:
    default:
        ESP_LOGI(TAG, "flow: nothing to do");
        break;
    }

    /* Centralized sleep preparation. Radio is already asleep after
     * LoRa close / Wi-Fi session; make the invariant explicit. */
    ensure_radio_asleep();

    diagnostics_t diag;
    diagnostics_get(&diag);
    ESP_LOGI(TAG, "diag: wifi=%lu mqtt=%lu lora=%lu sensor=%lu",
             (unsigned long)diag.wifi_failures, (unsigned long)diag.mqtt_failures,
             (unsigned long)diag.lora_failures, (unsigned long)diag.sensor_failures);

    if (power_manager_prepare_sleep(s_app.power, 0) != ESP_OK) {
        ESP_LOGE(TAG, "sleep preparation failed");
    }
    power_manager_enter_deep_sleep();
}
