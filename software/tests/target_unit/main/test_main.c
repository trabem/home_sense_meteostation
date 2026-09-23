#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "unity_test_runner.h"

#include "app_flow.h"
#include "telemetry.h"
#include "telemetry_codec.h"

#define TEST_DEVICE_ID  "meteo-a17c42"
#define TEST_FW_VERSION "1.2.3"
#define TEST_TS         1780000000LL

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

static uint32_t ref_fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s != '\0') {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
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
    t.transport = TRANSPORT_MODE_LORA;
    t.boot_count = 7;
    t.reset_reason = 1;
    t.wake_reason = 2;
    return t;
}

TEST_CASE("device hash is FNV-1a", "[codec]")
{
    TEST_ASSERT_EQUAL_UINT32(ref_fnv1a(TEST_DEVICE_ID),
                             telemetry_codec_device_hash(TEST_DEVICE_ID));
}

TEST_CASE("mqtt telemetry json contains all sections", "[codec]")
{
    char buf[TELEMETRY_CODEC_JSON_BUF_SIZE];
    size_t len = 0;
    telemetry_t t = full_telemetry();

    TEST_ASSERT_EQUAL(ESP_OK, telemetry_codec_encode_mqtt(&t, TEST_DEVICE_ID,
                      TEST_FW_VERSION, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL(strlen(buf), len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"device_id\":\"meteo-a17c42\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"environment\":{\"temperature_c\":21.50,\"humidity_percent\":63.25,\"pressure_pa\":100821}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"battery\":{\"voltage_v\":3.250,\"current_a\":-0.125,\"power_w\":0.250}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"solar\":{\"voltage_v\":5.500,\"current_a\":0.500,\"power_w\":2.750}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"charging\":true"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"boot_count\":7"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"firmware\":\"1.2.3\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"transport\":\"lora\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"reset_reason\":1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"wake_reason\":2"));
}

TEST_CASE("mqtt telemetry json omits invalid sections", "[codec]")
{
    char buf[TELEMETRY_CODEC_JSON_BUF_SIZE];
    size_t len = 0;
    telemetry_t t = full_telemetry();
    t.environment_valid = false;
    t.solar_valid = false;
    t.transport = TRANSPORT_MODE_WIFI;

    TEST_ASSERT_EQUAL(ESP_OK, telemetry_codec_encode_mqtt(&t, TEST_DEVICE_ID,
                      TEST_FW_VERSION, buf, sizeof(buf), &len));
    TEST_ASSERT_NULL(strstr(buf, "\"environment\""));
    TEST_ASSERT_NULL(strstr(buf, "\"solar\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"battery\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"transport\":\"wifi\""));
}

TEST_CASE("mqtt lightning json with and without energy", "[codec]")
{
    char buf[TELEMETRY_CODEC_JSON_BUF_SIZE];
    size_t len = 0;
    lightning_event_t ev = {
        .timestamp = TEST_TS, .distance_km = 12, .energy = 123456, .has_energy = true,
    };

    TEST_ASSERT_EQUAL(ESP_OK, lightning_codec_encode_mqtt(&ev, TEST_DEVICE_ID,
                      buf, sizeof(buf), &len));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"lightning\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"distance_km\":12"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"energy\":123456"));

    ev.has_energy = false;
    TEST_ASSERT_EQUAL(ESP_OK, lightning_codec_encode_mqtt(&ev, TEST_DEVICE_ID,
                      buf, sizeof(buf), &len));
    TEST_ASSERT_NULL(strstr(buf, "energy"));
}

TEST_CASE("mqtt boot json", "[codec]")
{
    char buf[TELEMETRY_CODEC_JSON_BUF_SIZE];
    size_t len = 0;
    boot_info_t info = {
        .timestamp = TEST_TS, .boot_count = 42, .reset_reason = 3, .wake_reason = 1,
    };

    TEST_ASSERT_EQUAL(ESP_OK, boot_codec_encode_mqtt(&info, TEST_DEVICE_ID,
                      TEST_FW_VERSION, buf, sizeof(buf), &len));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"boot\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"firmware\":\"1.2.3\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"boot_count\":42"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"reset_reason\":3"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"wake_reason\":1"));
}

TEST_CASE("lora telemetry full frame layout", "[codec]")
{
    telemetry_t t = full_telemetry();
    uint32_t hash = telemetry_codec_device_hash(TEST_DEVICE_ID);
    uint8_t buf[TELEMETRY_LORA_TELEMETRY_MAX];
    uint8_t expected[TELEMETRY_LORA_TELEMETRY_MAX];
    size_t len = 0;

    expected[0] = 1;
    expected[1] = 0x01;
    put_u32le(expected + 2, hash);
    put_u32le(expected + 6, (uint32_t)TEST_TS);
    expected[10] = 0x0F;
    put_u16le(expected + 11, 2150);
    put_u32le(expected + 13, 100821);
    put_u16le(expected + 17, 6325);
    put_u16le(expected + 19, 3250);
    put_u32le(expected + 21, (uint32_t)-125);
    put_u32le(expected + 25, 250);
    put_u16le(expected + 29, 5500);
    put_u32le(expected + 31, 500);
    put_u32le(expected + 35, 2750);

    TEST_ASSERT_EQUAL(ESP_OK, telemetry_codec_encode_lora(&t, hash, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL_UINT32(39, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, 39);
}

TEST_CASE("lora telemetry battery-only frame", "[codec]")
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
    expected[10] = 0x02;
    put_u16le(expected + 11, 3250);
    put_u32le(expected + 13, (uint32_t)-125);
    put_u32le(expected + 17, 250);

    TEST_ASSERT_EQUAL(ESP_OK, telemetry_codec_encode_lora(&t, hash, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL_UINT32(21, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, 21);
}

TEST_CASE("lora lightning frames", "[codec]")
{
    uint32_t hash = telemetry_codec_device_hash(TEST_DEVICE_ID);
    uint8_t buf[TELEMETRY_LORA_LIGHTNING_MAX];
    uint8_t expected[TELEMETRY_LORA_LIGHTNING_MAX];
    size_t len = 0;
    lightning_event_t ev = {
        .timestamp = TEST_TS, .distance_km = 12, .energy = 123456, .has_energy = true,
    };

    expected[0] = 1;
    expected[1] = 0x02;
    put_u32le(expected + 2, hash);
    put_u32le(expected + 6, (uint32_t)TEST_TS);
    expected[10] = 12;
    expected[11] = 0x01;
    put_u32le(expected + 12, 123456);

    TEST_ASSERT_EQUAL(ESP_OK, lightning_codec_encode_lora(&ev, hash, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL_UINT32(16, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, 16);

    ev.has_energy = false;
    TEST_ASSERT_EQUAL(ESP_OK, lightning_codec_encode_lora(&ev, hash, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL_UINT32(12, len);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[11]);
}

TEST_CASE("lora boot frame", "[codec]")
{
    uint32_t hash = telemetry_codec_device_hash(TEST_DEVICE_ID);
    uint8_t buf[TELEMETRY_LORA_BOOT_MAX];
    uint8_t expected[TELEMETRY_LORA_BOOT_MAX];
    size_t len = 0;
    boot_info_t info = {
        .timestamp = TEST_TS, .boot_count = 42, .reset_reason = 3, .wake_reason = 1,
    };

    expected[0] = 1;
    expected[1] = 0x03;
    put_u32le(expected + 2, hash);
    put_u32le(expected + 6, (uint32_t)TEST_TS);
    put_u32le(expected + 10, 42);
    expected[14] = (uint8_t)(3 | (1 << 4));

    TEST_ASSERT_EQUAL(ESP_OK, boot_codec_encode_lora(&info, hash, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL_UINT32(15, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, 15);
}

TEST_CASE("codec rejects too-small buffers", "[codec]")
{
    telemetry_t t = full_telemetry();
    lightning_event_t ev = {
        .timestamp = TEST_TS, .distance_km = 1, .energy = 1, .has_energy = true,
    };
    boot_info_t info = { .timestamp = TEST_TS, .boot_count = 1 };
    uint32_t hash = telemetry_codec_device_hash(TEST_DEVICE_ID);
    uint8_t bin[8];
    char json[16];
    size_t len = 0;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      telemetry_codec_encode_lora(&t, hash, bin, sizeof(bin), &len));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      lightning_codec_encode_lora(&ev, hash, bin, sizeof(bin), &len));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      boot_codec_encode_lora(&info, hash, bin, sizeof(bin), &len));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      telemetry_codec_encode_mqtt(&t, TEST_DEVICE_ID, TEST_FW_VERSION,
                                                  json, sizeof(json), &len));
}

TEST_CASE("app_flow_decide routes wake reasons", "[app_flow]")
{
    TEST_ASSERT_EQUAL(APP_ACTION_TELEMETRY, app_flow_decide(APP_WAKE_POWER_ON, false));
    TEST_ASSERT_EQUAL(APP_ACTION_TELEMETRY, app_flow_decide(APP_WAKE_TIMER, false));
    TEST_ASSERT_EQUAL(APP_ACTION_TELEMETRY, app_flow_decide(APP_WAKE_UNKNOWN, false));
    TEST_ASSERT_EQUAL(APP_ACTION_LIGHTNING, app_flow_decide(APP_WAKE_LIGHTNING_IRQ, true));
    TEST_ASSERT_EQUAL(APP_ACTION_SLEEP, app_flow_decide(APP_WAKE_LIGHTNING_IRQ, false));
}

TEST_CASE("app_flow_transport_from_j4 mapping", "[app_flow]")
{
    TEST_ASSERT_EQUAL(TRANSPORT_MODE_WIFI, app_flow_transport_from_j4(true, true));
    TEST_ASSERT_EQUAL(TRANSPORT_MODE_LORA, app_flow_transport_from_j4(false, true));
    TEST_ASSERT_EQUAL(TRANSPORT_MODE_LORA, app_flow_transport_from_j4(true, false));
    TEST_ASSERT_EQUAL(TRANSPORT_MODE_WIFI, app_flow_transport_from_j4(false, false));
}

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_menu();
    UNITY_END();
}
