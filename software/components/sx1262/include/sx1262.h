#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * SX1262 driver for the Ebyte E22-900M22S module (TZ 20-23).
 *
 * TX-only usage. RF switch control via DIO2 (SetDIO2AsRfSwitchCtrl), as the
 * PCB routes E22 DIO2 -> LORA_TXEN; there is no MCU GPIO for RXEN.
 * All waits (BUSY, TX_DONE) are bounded by timeouts.
 */

typedef struct sx1262 *sx1262_handle_t;

typedef struct {
    spi_host_device_t spi_host;
    gpio_num_t mosi_gpio;
    gpio_num_t miso_gpio;
    gpio_num_t sck_gpio;
    gpio_num_t nss_gpio;
    gpio_num_t busy_gpio;
    gpio_num_t nrst_gpio;
    gpio_num_t dio1_gpio;

    uint32_t frequency_hz;      /* CONFIG_METEO_LORA_FREQUENCY_HZ */
    int8_t   tx_power_dbm;      /* CONFIG_METEO_LORA_TX_POWER_DBM */
    uint8_t  spreading_factor;  /* 5..12 */
    uint32_t bandwidth_hz;      /* 125000 / 250000 / 500000 */
    uint8_t  coding_rate;       /* 1=4/5 .. 4=4/8 */

    uint32_t tx_timeout_ms;     /* TX_DONE wait limit */
    uint32_t busy_timeout_ms;   /* BUSY line wait limit */
} sx1262_config_t;

/** Allocates the context, attaches the SPI device, hooks the DIO1 ISR. */
esp_err_t sx1262_create(const sx1262_config_t *config, sx1262_handle_t *out_handle);

/**
 * Reset, then configure: standby, DIO2 as RF switch, packet type LoRa,
 * frequency, modulation params, packet params, PA config, TX params.
 */
esp_err_t sx1262_init(sx1262_handle_t handle);

/**
 * Blocking transmit: standby -> write buffer -> SetTx -> wait DIO1 TX_DONE
 * (bounded by tx_timeout_ms). Returns ESP_ERR_TIMEOUT on timeout.
 */
esp_err_t sx1262_send(sx1262_handle_t handle, const uint8_t *data, size_t length);

/** SetSleep (warm start). ~180 nA on E22-900M22S; use whenever idle. */
esp_err_t sx1262_sleep(sx1262_handle_t handle);

esp_err_t sx1262_destroy(sx1262_handle_t handle);

#ifdef __cplusplus
}
#endif
