#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Wire serialization (TZ 26-27). Transports never build payloads; they only
 * receive opaque buffers produced here.
 *
 * MQTT payload: JSON text.
 * LoRa payload: compact binary, little-endian, versioned:
 *
 *   Telemetry (max 39 bytes):
 *     [0]      protocol version (=1)
 *     [1]      message type (=0x01)
 *     [2..5]   device hash (u32, FNV-1a of device id string)
 *     [6..9]   unix timestamp (u32)
 *     [10]     flags: bit0 env_valid, bit1 battery_valid, bit2 solar_valid,
 *              bit3 charging, bit4 charge_complete, bit5 transport(0=LoRa,1=Wi-Fi)
 *     then, only when the corresponding valid bit is set:
 *       env:     i16 temp*100 C | u32 pressure Pa | u16 rh*100 %     (8 B)
 *       battery: u16 mV | i32 mA | i32 mW                            (10 B)
 *       solar:   u16 mV | i32 mA | i32 mW                            (10 B)
 *
 *   Lightning (12 or 16 bytes):
 *     [0] version, [1] type(=0x02), [2..5] device hash, [6..9] timestamp,
 *     [10] distance_km, [11] flags: bit0 has_energy,
 *     [12..15] energy (u32) when has_energy
 *
 *   Boot (15 bytes):
 *     [0] version, [1] type(=0x03), [2..5] device hash, [6..9] timestamp,
 *     [10..13] boot_count (u32), [14] reset_reason | wake_reason << 4
 */

#define TELEMETRY_LORA_PROTOCOL_VERSION 1u

#define TELEMETRY_CODEC_JSON_BUF_SIZE   512u
#define TELEMETRY_LORA_TELEMETRY_MAX    39u
#define TELEMETRY_LORA_LIGHTNING_MAX    16u
#define TELEMETRY_LORA_BOOT_MAX         15u

/** FNV-1a 32-bit hash of the device id string, used in LoRa frames. */
uint32_t telemetry_codec_device_hash(const char *device_id);

esp_err_t telemetry_codec_encode_mqtt(const telemetry_t *telemetry,
                                      const char *device_id,
                                      const char *fw_version,
                                      char *buf, size_t buf_size, size_t *out_len);

esp_err_t telemetry_codec_encode_lora(const telemetry_t *telemetry,
                                      uint32_t device_hash,
                                      uint8_t *buf, size_t buf_size, size_t *out_len);

esp_err_t lightning_codec_encode_mqtt(const lightning_event_t *event,
                                      const char *device_id,
                                      char *buf, size_t buf_size, size_t *out_len);

esp_err_t lightning_codec_encode_lora(const lightning_event_t *event,
                                      uint32_t device_hash,
                                      uint8_t *buf, size_t buf_size, size_t *out_len);

esp_err_t boot_codec_encode_mqtt(const boot_info_t *info,
                                 const char *device_id,
                                 const char *fw_version,
                                 char *buf, size_t buf_size, size_t *out_len);

esp_err_t boot_codec_encode_lora(const boot_info_t *info,
                                 uint32_t device_hash,
                                 uint8_t *buf, size_t buf_size, size_t *out_len);

#ifdef __cplusplus
}
#endif
