/**
 * Hardware integration test build.
 *
 * Runs a sequential checklist against the real board and prints PASS/FAIL
 * per item on the monitor. Ends with a 10 s deep-sleep timer wake test;
 * the WAKE_IRQ (AS3935) wake test is armed after that, so the following
 * sleep only exits on a real lightning/disturber interrupt.
 *
 * Expected I2C scan, main board only: 0x40 (INA226 solar), 0x41 (PCA9536),
 * 0x44 (INA226 battery), 0x68 (DS3231). With sensor board attached also
 * 0x03 (AS3935) and 0x76 (BME280). FRAM is not populated in V1.
 */

#include <stdio.h>
#include <string.h>

#include "as3935.h"
#include "bme280.h"
#include "board.h"
#include "ds3231.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "ina226.h"
#include "pca9536.h"
#include "power_manager.h"
#include "sdkconfig.h"
#include "sx1262.h"
#include "telemetry.h"

static const char *TAG = "HWTEST";

static int s_pass;
static int s_fail;

#define CHECK(name, cond)                                    \
    do {                                                     \
        if (cond) {                                          \
            s_pass++;                                        \
            printf("[PASS] %s\n", name);                     \
        } else {                                             \
            s_fail++;                                        \
            printf("[FAIL] %s\n", name);                     \
        }                                                    \
    } while (0)

static void test_i2c_scan(void)
{
    printf("--- I2C scan ---\n");
    int found = 0;
    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
        if (i2c_master_probe(i2c_bus_get_handle(), addr,
                             CONFIG_METEO_I2C_TIMEOUT_MS) == ESP_OK) {
            printf("  found device at 0x%02X\n", addr);
            found++;
        }
    }
    printf("  %d device(s) on the bus\n", found);
    CHECK("i2c scan finds main board devices", found >= 3);
}

static void test_bme280(void)
{
    const bme280_config_t config = BME280_CONFIG_DEFAULT();
    bme280_handle_t bme = NULL;
    if (bme280_create(i2c_bus_get_handle(), &config, &bme) != ESP_OK) {
        CHECK("bme280 read (sensor board attached?)", false);
        return;
    }
    environment_measurement_t m = {0};
    const esp_err_t err = bme280_read_forced(bme, &m);
    printf("  T=%.2f C  P=%.0f Pa  RH=%.1f %%\n", m.temperature_c, m.pressure_pa,
           m.humidity_percent);
    CHECK("bme280 read", err == ESP_OK);
    bme280_sleep(bme);
    bme280_destroy(bme);
}

static void test_as3935(void)
{
    const as3935_config_t config = AS3935_CONFIG_DEFAULT();
    as3935_handle_t as = NULL;
    if (as3935_create(i2c_bus_get_handle(), &config, &as) != ESP_OK) {
        CHECK("as3935 register communication (sensor board attached?)", false);
        return;
    }
    as3935_irq_reason_t reason = AS3935_IRQ_NONE;
    const esp_err_t err = as3935_get_interrupt_reason(as, &reason);
    printf("  irq reason=0x%02X\n", (unsigned)reason);
    CHECK("as3935 register communication", err == ESP_OK);
    as3935_clear_statistics(as);
    as3935_destroy(as);
}

static void test_ds3231(void)
{
    const ds3231_config_t config = DS3231_CONFIG_DEFAULT();
    ds3231_handle_t rtc = NULL;
    CHECK("ds3231 create", ds3231_create(i2c_bus_get_handle(), &config, &rtc) == ESP_OK);
    if (rtc == NULL) {
        return;
    }
    struct tm before = {0};
    const bool read_ok = ds3231_get_time(rtc, &before) == ESP_OK;
    printf("  time=%04d-%02d-%02d %02d:%02d:%02d\n", before.tm_year + 1900,
           before.tm_mon + 1, before.tm_mday, before.tm_hour, before.tm_min, before.tm_sec);
    CHECK("ds3231 read", read_ok);
    if (read_ok) {
        CHECK("ds3231 write/readback", ds3231_set_time(rtc, &before) == ESP_OK);
    }
    ds3231_destroy(rtc);
}

static void test_ina226(const char *name, uint8_t address, float shunt_ohm)
{
    const ina226_config_t config = {
        .i2c_address = address,
        .shunt_resistance_ohm = shunt_ohm,
        .max_expected_current_a = 2.0f,
    };
    ina226_handle_t ina = NULL;
    char label[48];
    snprintf(label, sizeof(label), "ina226 %s read", name);
    if (ina226_create(i2c_bus_get_handle(), &config, &ina) != ESP_OK) {
        CHECK(label, false);
        return;
    }
    power_measurement_t m = {0};
    const esp_err_t err = ina226_read(ina, &m);
    printf("  %s: U=%.3f V  I=%.3f A  P=%.3f W\n", name, m.bus_voltage_v, m.current_a,
           m.power_w);
    CHECK(label, err == ESP_OK);
    CHECK("ina226 power down", ina226_power_down(ina) == ESP_OK);
    ina226_destroy(ina);
}

static void test_pca9536_and_board(void)
{
    transport_mode_t mode = TRANSPORT_MODE_LORA;
    CHECK("board transport mode (J4)", board_get_transport_mode(&mode) == ESP_OK);
    printf("  J4 -> %s\n", mode == TRANSPORT_MODE_WIFI ? "wifi" : "lora");

    charge_state_t charge = CHARGE_STATE_UNKNOWN;
    CHECK("board charge state", board_get_charge_state(&charge) == ESP_OK);
    printf("  charge state=%d\n", (int)charge);

    power_source_t source = POWER_SOURCE_UNKNOWN;
    CHECK("board power source", board_get_power_source(&source) == ESP_OK);
    printf("  power source=%d\n", (int)source);

    char device_id[BOARD_DEVICE_ID_MAX_LEN];
    CHECK("device id", board_get_device_id(device_id, sizeof(device_id)) == ESP_OK);
    printf("  device id=%s\n", device_id);
}

static sx1262_handle_t s_radio;

static bool create_radio(void)
{
    const sx1262_config_t config = {
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
        .bandwidth_hz = 125000,
        .coding_rate = 1,
        .tx_timeout_ms = CONFIG_METEO_LORA_TX_TIMEOUT_MS,
        .busy_timeout_ms = CONFIG_METEO_LORA_BUSY_TIMEOUT_MS,
    };
    return sx1262_create(&config, &s_radio) == ESP_OK;
}

static void test_lora(void)
{
    CHECK("sx1262 create (SPI)", create_radio());
    if (s_radio == NULL) {
        return;
    }
    CHECK("sx1262 init (reset, DIO2 rf switch, params)", sx1262_init(s_radio) == ESP_OK);
    CHECK("sx1262 sleep", sx1262_sleep(s_radio) == ESP_OK);

    const uint8_t frame[] = { 0x01, 0x03, 'H', 'W', 'T', 'E', 'S', 'T' };
    const esp_err_t tx_err = sx1262_send(s_radio, frame, sizeof(frame));
    CHECK("sx1262 wake + TX (check spectrum/airflow on receiver side)", tx_err == ESP_OK);
    CHECK("sx1262 sleep after TX", sx1262_sleep(s_radio) == ESP_OK);
}

static void deep_sleep_timer_test(void)
{
    printf("--- deep sleep timer wake test: sleeping 10 s ---\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_sleep_enable_timer_wakeup(10ULL * 1000000ULL);
    esp_deep_sleep_start();
}

static void wake_irq_test(void)
{
    printf("--- WAKE_IRQ test: arming GPIO%d HIGH wake, sleeping ---\n",
           (int)BOARD_GPIO_WAKE_IRQ);
    printf("    (trigger the AS3935 or drive the line HIGH to wake)\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_wakeup_enable(BOARD_GPIO_WAKE_IRQ, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_deep_sleep_start();
}

void app_main(void)
{
    printf("\n===== meteostation hardware tests =====\n");

    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        CHECK("deep sleep timer wake", true);
        wake_irq_test();  /* does not return */
    }
    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        CHECK("WAKE_IRQ gpio wake", true);
        printf("===== wake tests done =====\n");
        /* fall through to the full checklist on the same boot */
    }

    const i2c_bus_config_t i2c_config = {
        .sda_gpio = BOARD_GPIO_I2C_SDA,
        .scl_gpio = BOARD_GPIO_I2C_SCL,
        .frequency_hz = CONFIG_METEO_I2C_FREQUENCY_HZ,
    };
    CHECK("i2c bus init", i2c_bus_init(&i2c_config) == ESP_OK);
    CHECK("board init", board_init(i2c_bus_get_handle()) == ESP_OK);

    test_i2c_scan();
    test_bme280();
    test_as3935();
    test_ds3231();
    test_ina226("battery", BOARD_INA226_BATTERY_ADDRESS, BOARD_INA226_BATTERY_SHUNT_OHM);
    test_ina226("solar", BOARD_INA226_SOLAR_ADDRESS, BOARD_INA226_SOLAR_SHUNT_OHM);
    test_pca9536_and_board();
    test_lora();

    printf("===== %d passed, %d failed =====\n", s_pass, s_fail);
    printf("(Wi-Fi provisioning / reconnect / MQTT publish are covered by the\n"
           " main firmware flow in TRANSPORT_MODE_WIFI with a reachable broker.)\n");

    if (s_radio != NULL) {
        sx1262_destroy(s_radio);
    }
    deep_sleep_timer_test();  /* does not return */
}
