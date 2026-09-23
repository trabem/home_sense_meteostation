#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "telemetry.h"
#include "telemetry_codec.h"

#define FNV1A_OFFSET_BASIS 2166136261u
#define FNV1A_PRIME        16777619u

typedef struct {
    char *cur;
    size_t remaining;
} json_writer_t;

#define JSON_PRINTF(w, ...)                                                \
    do {                                                                   \
        int n_ = snprintf((w)->cur, (w)->remaining, __VA_ARGS__);          \
        if (n_ < 0 || (size_t)n_ >= (w)->remaining) {                      \
            return ESP_ERR_INVALID_SIZE;                                   \
        }                                                                  \
        (w)->cur += n_;                                                    \
        (w)->remaining -= (size_t)n_;                                      \
    } while (0)

uint32_t telemetry_codec_device_hash(const char *device_id)
{
    uint32_t hash = FNV1A_OFFSET_BASIS;
    if (device_id == NULL) {
        return hash;
    }
    while (*device_id != '\0') {
        hash ^= (uint8_t)*device_id++;
        hash *= FNV1A_PRIME;
    }
    return hash;
}

static int32_t clamp_i32(double value, double lo, double hi)
{
    if (value != value) {
        return 0;
    }
    if (value >= hi) {
        return (int32_t)hi;
    }
    if (value <= lo) {
        return (int32_t)lo;
    }
    return (int32_t)value;
}

static int16_t sat_i16(double value)
{
    return (int16_t)clamp_i32(value, -32768.0, 32767.0);
}

static uint16_t sat_u16(double value)
{
    return (uint16_t)clamp_i32(value, 0.0, 65535.0);
}

static uint32_t sat_u32(double value)
{
    if (value != value || value <= 0.0) {
        return 0u;
    }
    if (value >= 4294967295.0) {
        return 4294967295u;
    }
    return (uint32_t)value;
}

static int32_t sat_milli(double value)
{
    return clamp_i32(value, -2147483648.0, 2147483647.0);
}

static void wr_u16le(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)(v & 0xFFu);
    dst[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void wr_u32le(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v & 0xFFu);
    dst[1] = (uint8_t)((v >> 8) & 0xFFu);
    dst[2] = (uint8_t)((v >> 16) & 0xFFu);
    dst[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void wr_header(uint8_t *dst, uint8_t type, uint32_t device_hash, int64_t timestamp)
{
    dst[0] = (uint8_t)TELEMETRY_LORA_PROTOCOL_VERSION;
    dst[1] = type;
    wr_u32le(dst + 2, device_hash);
    wr_u32le(dst + 6, (uint32_t)timestamp);
}

static void wr_power_section(uint8_t *dst, const power_measurement_t *power)
{
    wr_u16le(dst, sat_u16((double)power->bus_voltage_v * 1000.0));
    wr_u32le(dst + 2, (uint32_t)sat_milli((double)power->current_a * 1000.0));
    wr_u32le(dst + 6, (uint32_t)sat_milli((double)power->power_w * 1000.0));
}

esp_err_t telemetry_codec_encode_mqtt(const telemetry_t *telemetry,
                                      const char *device_id,
                                      const char *fw_version,
                                      char *buf, size_t buf_size, size_t *out_len)
{
    if (telemetry == NULL || device_id == NULL || fw_version == NULL ||
        buf == NULL || out_len == NULL || buf_size == 0u) {
        return ESP_ERR_INVALID_ARG;
    }

    json_writer_t w = { .cur = buf, .remaining = buf_size };

    JSON_PRINTF(&w, "{\"schema\":1,\"device_id\":\"%s\",\"timestamp\":%lld",
                device_id, (long long)telemetry->timestamp);

    if (telemetry->environment_valid) {
        JSON_PRINTF(&w,
                    ",\"environment\":{\"temperature_c\":%.2f,\"humidity_percent\":%.2f,\"pressure_pa\":%.0f}",
                    (double)telemetry->environment.temperature_c,
                    (double)telemetry->environment.humidity_percent,
                    (double)telemetry->environment.pressure_pa);
    }
    if (telemetry->battery_valid) {
        JSON_PRINTF(&w,
                    ",\"battery\":{\"voltage_v\":%.3f,\"current_a\":%.3f,\"power_w\":%.3f}",
                    (double)telemetry->battery.bus_voltage_v,
                    (double)telemetry->battery.current_a,
                    (double)telemetry->battery.power_w);
    }
    if (telemetry->solar_valid) {
        JSON_PRINTF(&w,
                    ",\"solar\":{\"voltage_v\":%.3f,\"current_a\":%.3f,\"power_w\":%.3f}",
                    (double)telemetry->solar.bus_voltage_v,
                    (double)telemetry->solar.current_a,
                    (double)telemetry->solar.power_w);
    }

    JSON_PRINTF(&w,
                ",\"charging\":%s,\"boot_count\":%lu,\"firmware\":\"%s\",\"transport\":\"%s\",\"reset_reason\":%u,\"wake_reason\":%u}",
                telemetry->charging ? "true" : "false",
                (unsigned long)telemetry->boot_count,
                fw_version,
                telemetry->transport == TRANSPORT_MODE_WIFI ? "wifi" : "lora",
                (unsigned)telemetry->reset_reason,
                (unsigned)telemetry->wake_reason);

    *out_len = (size_t)(w.cur - buf);
    return ESP_OK;
}

esp_err_t lightning_codec_encode_mqtt(const lightning_event_t *event,
                                      const char *device_id,
                                      char *buf, size_t buf_size, size_t *out_len)
{
    if (event == NULL || device_id == NULL || buf == NULL || out_len == NULL ||
        buf_size == 0u) {
        return ESP_ERR_INVALID_ARG;
    }

    json_writer_t w = { .cur = buf, .remaining = buf_size };

    JSON_PRINTF(&w,
                "{\"schema\":1,\"type\":\"lightning\",\"device_id\":\"%s\",\"timestamp\":%lld,\"distance_km\":%u",
                device_id, (long long)event->timestamp, (unsigned)event->distance_km);
    if (event->has_energy) {
        JSON_PRINTF(&w, ",\"energy\":%lu", (unsigned long)event->energy);
    }
    JSON_PRINTF(&w, "}");

    *out_len = (size_t)(w.cur - buf);
    return ESP_OK;
}

esp_err_t boot_codec_encode_mqtt(const boot_info_t *info,
                                 const char *device_id,
                                 const char *fw_version,
                                 char *buf, size_t buf_size, size_t *out_len)
{
    if (info == NULL || device_id == NULL || fw_version == NULL ||
        buf == NULL || out_len == NULL || buf_size == 0u) {
        return ESP_ERR_INVALID_ARG;
    }

    json_writer_t w = { .cur = buf, .remaining = buf_size };

    JSON_PRINTF(&w,
                "{\"schema\":1,\"type\":\"boot\",\"device_id\":\"%s\",\"firmware\":\"%s\",\"timestamp\":%lld,\"boot_count\":%lu,\"reset_reason\":%u,\"wake_reason\":%u}",
                device_id, fw_version, (long long)info->timestamp,
                (unsigned long)info->boot_count, (unsigned)info->reset_reason,
                (unsigned)info->wake_reason);

    *out_len = (size_t)(w.cur - buf);
    return ESP_OK;
}

esp_err_t telemetry_codec_encode_lora(const telemetry_t *telemetry,
                                      uint32_t device_hash,
                                      uint8_t *buf, size_t buf_size, size_t *out_len)
{
    if (telemetry == NULL || buf == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t needed = 11u;
    if (telemetry->environment_valid) {
        needed += 8u;
    }
    if (telemetry->battery_valid) {
        needed += 10u;
    }
    if (telemetry->solar_valid) {
        needed += 10u;
    }
    if (buf_size < needed) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t flags = 0u;
    if (telemetry->environment_valid) {
        flags |= 0x01u;
    }
    if (telemetry->battery_valid) {
        flags |= 0x02u;
    }
    if (telemetry->solar_valid) {
        flags |= 0x04u;
    }
    if (telemetry->charging) {
        flags |= 0x08u;
    }
    if (telemetry->charge_complete) {
        flags |= 0x10u;
    }
    if (telemetry->transport == TRANSPORT_MODE_WIFI) {
        flags |= 0x20u;
    }

    wr_header(buf, (uint8_t)MESSAGE_TYPE_TELEMETRY, device_hash, telemetry->timestamp);
    buf[10] = flags;

    uint8_t *dst = buf + 11;
    if (telemetry->environment_valid) {
        wr_u16le(dst, (uint16_t)sat_i16((double)telemetry->environment.temperature_c * 100.0));
        wr_u32le(dst + 2, sat_u32((double)telemetry->environment.pressure_pa));
        wr_u16le(dst + 6, sat_u16((double)telemetry->environment.humidity_percent * 100.0));
        dst += 8;
    }
    if (telemetry->battery_valid) {
        wr_power_section(dst, &telemetry->battery);
        dst += 10;
    }
    if (telemetry->solar_valid) {
        wr_power_section(dst, &telemetry->solar);
        dst += 10;
    }

    *out_len = (size_t)(dst - buf);
    return ESP_OK;
}

esp_err_t lightning_codec_encode_lora(const lightning_event_t *event,
                                      uint32_t device_hash,
                                      uint8_t *buf, size_t buf_size, size_t *out_len)
{
    if (event == NULL || buf == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t needed = event->has_energy ? 16u : 12u;
    if (buf_size < needed) {
        return ESP_ERR_INVALID_SIZE;
    }

    wr_header(buf, (uint8_t)MESSAGE_TYPE_LIGHTNING, device_hash, event->timestamp);
    buf[10] = event->distance_km;
    buf[11] = event->has_energy ? 0x01u : 0x00u;
    if (event->has_energy) {
        wr_u32le(buf + 12, event->energy);
    }

    *out_len = needed;
    return ESP_OK;
}

esp_err_t boot_codec_encode_lora(const boot_info_t *info,
                                 uint32_t device_hash,
                                 uint8_t *buf, size_t buf_size, size_t *out_len)
{
    if (info == NULL || buf == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (buf_size < 15u) {
        return ESP_ERR_INVALID_SIZE;
    }

    wr_header(buf, (uint8_t)MESSAGE_TYPE_BOOT, device_hash, info->timestamp);
    wr_u32le(buf + 10, info->boot_count);
    buf[14] = (uint8_t)((info->reset_reason & 0x0Fu) | (uint8_t)(info->wake_reason << 4));

    *out_len = 15u;
    return ESP_OK;
}
