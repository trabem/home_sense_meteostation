#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * AS3935 lightning sensor (TZ 9).
 *
 * The driver only reports facts: interrupt reason, estimated distance,
 * energy. It never decides what to do with a lightning event.
 *
 * Sensor board straps A0=HIGH, A1=HIGH -> I2C address 0x03.
 */

#define AS3935_I2C_ADDRESS_DEFAULT  0x03

typedef enum {
    AS3935_IRQ_NONE      = 0x00,
    AS3935_IRQ_NOISE     = 0x01,
    AS3935_IRQ_DISTURBER = 0x04,
    AS3935_IRQ_LIGHTNING = 0x08,
} as3935_irq_reason_t;

typedef struct as3935 *as3935_handle_t;

typedef struct {
    uint8_t i2c_address;
    bool indoor;            /* AFE gain: indoor (default) or outdoor */
    uint8_t noise_floor;    /* 0..7, watchdog threshold preset */
} as3935_config_t;

#define AS3935_CONFIG_DEFAULT() { \
    .i2c_address = AS3935_I2C_ADDRESS_DEFAULT, \
    .indoor = true, \
    .noise_floor = 2, \
}

/** Creates the device and applies configuration (AFE, thresholds, masks). */
esp_err_t as3935_create(i2c_master_bus_handle_t bus,
                        const as3935_config_t *config,
                        as3935_handle_t *out_handle);

esp_err_t as3935_get_interrupt_reason(as3935_handle_t handle,
                                      as3935_irq_reason_t *reason);

/** Estimated storm distance in km; 0x3F means "out of range". */
esp_err_t as3935_read_lightning_distance(as3935_handle_t handle,
                                         uint8_t *distance_km);

esp_err_t as3935_read_lightning_energy(as3935_handle_t handle, uint32_t *energy);

esp_err_t as3935_clear_statistics(as3935_handle_t handle);

esp_err_t as3935_destroy(as3935_handle_t handle);

#ifdef __cplusplus
}
#endif
