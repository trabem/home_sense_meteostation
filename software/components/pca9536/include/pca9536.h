#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * PCA9536 4-bit I2C GPIO expander.
 *
 * Used on the main board for CHRG_STATUS, STDBY_STATUS and the J4 transport
 * mode switch. This driver only knows pins; semantics live in `board`.
 */

#define PCA9536_I2C_ADDRESS_DEFAULT  0x41   /* fixed hardware address */

typedef struct pca9536 *pca9536_handle_t;

typedef struct {
    uint8_t i2c_address;
    uint8_t direction_mask;  /* 1 = input, written to the config register */
} pca9536_config_t;

#define PCA9536_CONFIG_DEFAULT() { \
    .i2c_address = PCA9536_I2C_ADDRESS_DEFAULT, \
    .direction_mask = 0x0F, \
}

esp_err_t pca9536_create(i2c_master_bus_handle_t bus,
                         const pca9536_config_t *config,
                         pca9536_handle_t *out_handle);

esp_err_t pca9536_read_port(pca9536_handle_t handle, uint8_t *value);
esp_err_t pca9536_read_pin(pca9536_handle_t handle, uint8_t pin, bool *level);

esp_err_t pca9536_destroy(pca9536_handle_t handle);

#ifdef __cplusplus
}
#endif
