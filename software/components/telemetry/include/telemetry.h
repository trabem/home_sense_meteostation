#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Domain/application data models (TZ 24-25, 38-39, 43).
 *
 * These types are pure data: no JSON, no transport, no hardware knowledge.
 * Drivers may fill measurement structs; serialization lives in
 * telemetry_codec; transports only move bytes.
 */

typedef struct {
    float temperature_c;
    float pressure_pa;
    float humidity_percent;
} environment_measurement_t;

typedef struct {
    float bus_voltage_v;
    float current_a;
    float power_w;
} power_measurement_t;

typedef enum {
    TRANSPORT_MODE_LORA = 0,
    TRANSPORT_MODE_WIFI,
} transport_mode_t;

typedef enum {
    CHARGE_STATE_UNKNOWN = 0,
    CHARGE_STATE_CHARGING,
    CHARGE_STATE_COMPLETE,
    CHARGE_STATE_IDLE,
    CHARGE_STATE_FAULT,
} charge_state_t;

typedef enum {
    POWER_SOURCE_UNKNOWN = 0,
    POWER_SOURCE_USB,
    POWER_SOURCE_SOLAR,
} power_source_t;

typedef enum {
    MESSAGE_TYPE_TELEMETRY = 0x01,
    MESSAGE_TYPE_LIGHTNING = 0x02,
    MESSAGE_TYPE_BOOT      = 0x03,
} message_type_t;

typedef struct {
    int64_t timestamp;                 /* unix time, seconds */

    environment_measurement_t environment;
    power_measurement_t battery;
    power_measurement_t solar;

    bool environment_valid;            /* partial telemetry support (TZ 43) */
    bool battery_valid;
    bool solar_valid;

    bool charging;
    bool charge_complete;

    transport_mode_t transport;

    /* status extras (TZ 24) */
    uint32_t boot_count;
    uint8_t reset_reason;              /* esp_reset_reason_t */
    uint8_t wake_reason;               /* app_wakeup_reason_t */
} telemetry_t;

typedef struct {
    int64_t timestamp;                 /* unix time, seconds */
    uint8_t distance_km;
    uint32_t energy;
    bool has_energy;
} lightning_event_t;

typedef struct {
    int64_t timestamp;
    uint32_t boot_count;
    uint8_t reset_reason;
    uint8_t wake_reason;
} boot_info_t;

#ifdef __cplusplus
}
#endif
