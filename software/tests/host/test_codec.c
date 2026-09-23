#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "telemetry.h"
#include "telemetry_codec.h"
#include "test_framework.h"

#define TEST_DEVICE_ID "meteo-a17c42"
#define TEST_FW_VERSION "1.2.3"
#define TEST_TS 1780000000LL

static uint32_t ref_fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s != '\0') {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

static void put_u16le(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)v;
    dst[1] = (uint8_t)(v >> 8);
}

static void put_u32le(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)v;
    dst[1] = (uint8_t)(v >> 8);
    dst[2] = (uint8_t)(v >> 16);
    dst[3] = (uint8_t)(v >> 24);
}

static telemetry_t full_telemetry(void)
{
    telemetry_t t;
    memset(&t, 0, sizeof(t));
    t.timestamp = TEST_TS;
    t.environment.temperature_c = 21.5f;
    t.environment.pressure_pa = 100821.0f;
    t.environment.humidity_percent = 63.25f;
    t.battery.bus_voltage_v = 3.25f;
    t.battery.current_a = -0.125f;
    t.battery.power_w = 0.25f;
    t.solar.bus_voltage_v = 5.5f;
    t.solar.current_a = 0.5f;
    t.solar.power_w = 2.75f;
    t.environment_valid = true;
    t.battery_valid = true;
    t.solar_valid = true;
    t.charging = true;
    t.charge_complete = false;
    t.transport = TRANSPORT_MODE_LORA;
    t.boot_count = 7;
    t.reset_reason = 1;
    t.wake_reason = 2;
    return t;
}

static void test_device_hash(void)
{
    CHECK_EQ_INT(ref_fnv1a(TEST_DEVICE_ID), telemetry_codec_device_hash(TEST_DEVICE_ID));
}

static void test_mqtt_telemetry_full(void)
{
    char buf[TELEMETRY_CODEC_JSON_BUF_SIZE];
    size_t len = 0;
    telemetry_t t = full_telemetry();

    esp_err_t err = telemetry_codec_encode_mqtt(&t, TEST_DEVICE_ID, TEST_FW_VERSION,
                                                buf, sizeof(buf), &len);
    CHECK_EQ_INT(ESP_OK, err);
    CHECK_EQ_INT(strlen(buf), len);

    CHECK_CONTAINS(buf, "\"schema\":1");
    CHECK_CONTAINS(buf, "\"device_id\":\"meteo-a17c42\"");
    CHECK_CONTAINS(buf, "\"timestamp\":1780000000");
    CHECK_CONTAINS(buf, "\"environment\":{\"temperature_c\":21.50,\"humidity_percent\":63.25,\"pressure_pa\":100821}");
    CHECK_CONTAINS(buf, "\"battery\":{\"voltage_v\":3.250,\"current_a\":-0.125,\"power_w\":0.250}");
    CHECK_CONTAINS(buf, "\"solar\":{\"voltage_v\":5.500,\"current_a\":0.500,\"power_w\":2.750}");
    CHECK_CONTAINS(buf, "\"charging\":true");
    CHECK_CONTAINS(buf, "\"boot_count\":7");
    CHECK_CONTAINS(buf, "\"firmware\":\"1.2.3\"");
    CHECK_CONTAINS(buf, "\"transport\":\"lora\"");
    CHECK_CONTAINS(buf, "\"reset_reason\":1");
    CHECK_CONTAINS(buf, "\"wake_reason\":2");
}

static void test_mqtt_telemetry_omits_invalid_sections(void)
{
    char buf[TELEMETRY_CODEC_JSON_BUF_SIZE];
    size_t len = 0;
    telemetry_t t = full_telemetry();
    t.environment_valid = false;
    t.solar_valid = false;
    t.charging = false;
    t.transport = TRANSPORT_MODE_WIFI;

    esp_err_t err = telemetry_codec_encode_mqtt(&t, TEST_DEVICE_ID, TEST_FW_VERSION,
                                                buf, sizeof(buf), &len);
    CHECK_EQ_INT(ESP_OK, err);
    CHECK_NOT_CONTAINS(buf, "\"environment\"");
    CHECK_NOT_CONTAINS(buf, "\"solar\"");
    CHECK_CONTAINS(buf, "\"battery\"");
    CHECK_CONTAINS(buf, "\"charging\":false");
    CHECK_CONTAINS(buf, "\"transport\":\"wifi\"");
}

static void test_mqtt_lightning(void)
{
    char buf[TELEMETRY_CODEC_JSON_BUF_SIZE];
    size_t len = 0;
    lightning_event_t ev = {
        .timestamp = TEST_TS,
        .distance_km = 12,
        .energy = 123456,
        .has_energy = true,
    };

    esp_err_t err = lightning_codec_encode_mqtt(&ev, TEST_DEVICE_ID, buf, sizeof(buf), &len);
    CHECK_EQ_INT(ESP_OK, err);
    CHECK_CONTAINS(buf, "\"type\":\"lightning\"");
    CHECK_CONTAINS(buf, "\"device_id\":\"meteo-a17c42\"");
    CHECK_CONTAINS(buf, "\"distance_km\":12");
    CHECK_CONTAINS(buf, "\"energy\":123456");

    ev.has_energy = false;
    err = lightning_codec_encode_mqtt(&ev, TEST_DEVICE_ID, buf, sizeof(buf), &len);
    CHECK_EQ_INT(ESP_OK, err);
    CHECK_NOT_CONTAINS(buf, "energy");
    CHECK_CONTAINS(buf, "\"distance_km\":12");
}

static void test_mqtt_boot(void)
{
    char buf[TELEMETRY_CODEC_JSON_BUF_SIZE];
    size_t len = 0;
    boot_info_t info = {
        .timestamp = TEST_TS,
        .boot_count = 42,
        .reset_reason = 3,
        .wake_reason = 1,
    };

    esp_err_t err = boot_codec_encode_mqtt(&info, TEST_DEVICE_ID, TEST_FW_VERSION,
                                           buf, sizeof(buf), &len);
    CHECK_EQ_INT(ESP_OK, err);
    CHECK_CONTAINS(buf, "\"schema\":1");
    CHECK_CONTAINS(buf, "\"type\":\"boot\"");
    CHECK_CONTAINS(buf, "\"device_id\":\"meteo-a17c42\"");
    CHECK_CONTAINS(buf, "\"firmware\":\"1.2.3\"");
    CHECK_CONTAINS(buf, "\"timestamp\":1780000000");
    CHECK_CONTAINS(buf, "\"boot_count\":42");
    CHECK_CONTAINS(buf, "\"reset_reason\":3");
    CHECK_CONTAINS(buf, "\"wake_reason\":1");
}

static void test_lora_telemetry_full_layout(void)
{
    telemetry_t t = full_telemetry();
    uint32_t hash = telemetry_codec_device_hash(TEST_DEVICE_ID);
    uint8_t buf[TELEMETRY_LORA_TELEMETRY_MAX];
    uint8_t expected[TELEMETRY_LORA_TELEMETRY_MAX];
    size_t len = 0;

    expected[0] = 1;      /* version */
    expected[1] = 0x01;   /* type telemetry */
    put_u32le(expected + 2, hash);
    put_u32le(expected + 6, (uint32_t)TEST_TS);
    expected[10] = 0x0F;  /* env|battery|solar|charging, lora */
    /* env: 21.5 C -> 2150, 100821 Pa, 63.25 % -> 6325 */
    put_u16le(expected + 11, 2150);
    put_u32le(expected + 13, 100821);
    put_u16le(expected + 17, 6325);
    /* battery: 3.25 V -> 3250 mV, -0.125 A -> -125 mA, 0.25 W -> 250 mW */
    put_u16le(expected + 19, 3250);
    put_u32le(expected + 21, (uint32_t)-125);
    put_u32le(expected + 25, 250);
    /* solar: 5.5 V -> 5500 mV, 0.5 A -> 500 mA, 2.75 W -> 2750 mW */
    put_u16le(expected + 29, 5500);
    put_u32le(expected + 31, 500);
    put_u32le(expected + 35, 2750);

    esp_err_t err = telemetry_codec_encode_lora(&t, hash, buf, sizeof(buf), &len);
    CHECK_EQ_INT(ESP_OK, err);
    CHECK_EQ_INT(39, len);
    CHECK_BYTES_EQ(expected, buf, 39);
}

static void test_lora_telemetry_battery_only(void)
{
    telemetry_t t = full_telemetry();
    t.environment_valid = false;
    t.solar_valid = false;
    t.charging = false;

    uint32_t hash = telemetry_codec_device_hash(TEST_DEVICE_ID);
    uint8_t buf[TELEMETRY_LORA_TELEMETRY_MAX];
    uint8_t expected[21];
    size_t len = 0;

    expected[0] = 1;
    expected[1] = 0x01;
    put_u32le(expected + 2, hash);
    put_u32le(expected + 6, (uint32_t)TEST_TS);
    expected[10] = 0x02;  /* battery bit only */
    put_u16le(expected + 11, 3250);
    put_u32le(expected + 13, (uint32_t)-125);
    put_u32le(expected + 17, 250);

    esp_err_t err = telemetry_codec_encode_lora(&t, hash, buf, sizeof(buf), &len);
    CHECK_EQ_INT(ESP_OK, err);
    CHECK_EQ_INT(21, len);
    CHECK_BYTES_EQ(expected, buf, 21);
}

static void test_lora_lightning(void)
{
    uint32_t hash = telemetry_codec_device_hash(TEST_DEVICE_ID);
    uint8_t buf[TELEMETRY_LORA_LIGHTNING_MAX];
    uint8_t expected[TELEMETRY_LORA_LIGHTNING_MAX];
    size_t len = 0;
    lightning_event_t ev = {
        .timestamp = TEST_TS,
        .distance_km = 12,
        .energy = 123456,
        .has_energy = true,
    };

    expected[0] = 1;
    expected[1] = 0x02;
    put_u32le(expected + 2, hash);
    put_u32le(expected + 6, (uint32_t)TEST_TS);
    expected[10] = 12;
    expected[11] = 0x01;
    put_u32le(expected + 12, 123456);

    esp_err_t err = lightning_codec_encode_lora(&ev, hash, buf, sizeof(buf), &len);
    CHECK_EQ_INT(ESP_OK, err);
    CHECK_EQ_INT(16, len);
    CHECK_BYTES_EQ(expected, buf, 16);

    ev.has_energy = false;
    err = lightning_codec_encode_lora(&ev, hash, buf, sizeof(buf), &len);
    CHECK_EQ_INT(ESP_OK, err);
    CHECK_EQ_INT(12, len);
    CHECK_BYTES_EQ(expected, buf, 11);
    CHECK_EQ_INT(0x00, buf[11]);
}

static void test_lora_boot(void)
{
    uint32_t hash = telemetry_codec_device_hash(TEST_DEVICE_ID);
    uint8_t buf[TELEMETRY_LORA_BOOT_MAX];
    uint8_t expected[TELEMETRY_LORA_BOOT_MAX];
    size_t len = 0;
    boot_info_t info = {
        .timestamp = TEST_TS,
        .boot_count = 42,
        .reset_reason = 3,
        .wake_reason = 1,
    };

    expected[0] = 1;
    expected[1] = 0x03;
    put_u32le(expected + 2, hash);
    put_u32le(expected + 6, (uint32_t)TEST_TS);
    put_u32le(expected + 10, 42);
    expected[14] = (uint8_t)(3 | (1 << 4));

    esp_err_t err = boot_codec_encode_lora(&info, hash, buf, sizeof(buf), &len);
    CHECK_EQ_INT(ESP_OK, err);
    CHECK_EQ_INT(15, len);
    CHECK_BYTES_EQ(expected, buf, 15);
}

static void test_buffer_too_small(void)
{
    telemetry_t t = full_telemetry();
    lightning_event_t ev = {
        .timestamp = TEST_TS, .distance_km = 1, .energy = 1, .has_energy = true,
    };
    boot_info_t info = { .timestamp = TEST_TS, .boot_count = 1, .reset_reason = 0, .wake_reason = 0 };
    uint32_t hash = telemetry_codec_device_hash(TEST_DEVICE_ID);
    uint8_t bin[8];
    char json[16];
    size_t len = 0;

    CHECK_EQ_INT(ESP_ERR_INVALID_SIZE,
                 telemetry_codec_encode_lora(&t, hash, bin, sizeof(bin), &len));
    CHECK_EQ_INT(ESP_ERR_INVALID_SIZE,
                 lightning_codec_encode_lora(&ev, hash, bin, sizeof(bin), &len));
    CHECK_EQ_INT(ESP_ERR_INVALID_SIZE,
                 boot_codec_encode_lora(&info, hash, bin, sizeof(bin), &len));
    CHECK_EQ_INT(ESP_ERR_INVALID_SIZE,
                 telemetry_codec_encode_mqtt(&t, TEST_DEVICE_ID, TEST_FW_VERSION,
                                             json, sizeof(json), &len));
    CHECK_EQ_INT(ESP_ERR_INVALID_SIZE,
                 lightning_codec_encode_mqtt(&ev, TEST_DEVICE_ID, json, sizeof(json), &len));
    CHECK_EQ_INT(ESP_ERR_INVALID_SIZE,
                 boot_codec_encode_mqtt(&info, TEST_DEVICE_ID, TEST_FW_VERSION,
                                        json, sizeof(json), &len));

    CHECK_EQ_INT(ESP_ERR_INVALID_ARG,
                 telemetry_codec_encode_lora(NULL, hash, bin, sizeof(bin), &len));
    CHECK_EQ_INT(ESP_ERR_INVALID_ARG,
                 telemetry_codec_encode_lora(&t, hash, NULL, sizeof(bin), &len));
    CHECK_EQ_INT(ESP_ERR_INVALID_ARG,
                 telemetry_codec_encode_mqtt(&t, TEST_DEVICE_ID, TEST_FW_VERSION,
                                             NULL, sizeof(json), &len));
}

int main(void)
{
    test_device_hash();
    test_mqtt_telemetry_full();
    test_mqtt_telemetry_omits_invalid_sections();
    test_mqtt_lightning();
    test_mqtt_boot();
    test_lora_telemetry_full_layout();
    test_lora_telemetry_battery_only();
    test_lora_lightning();
    test_lora_boot();
    test_buffer_too_small();
    TEST_SUMMARY_AND_RETURN();
}
