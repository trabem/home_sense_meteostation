#pragma once

#include <stddef.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Board component: the single place that knows the physical PCB.
 *
 * Owns the PCA9536 (J4 transport selector, charger status lines) and the
 * direct GPIO inputs (WAKE_IRQ, CHARGE_SOURCE_STATUS_OUT). Everything else
 * in the firmware asks board_* instead of touching GPIO numbers.
 */

/* SPI / LoRa (E22-900M22S) */
#define BOARD_GPIO_SPI_MOSI      GPIO_NUM_4
#define BOARD_GPIO_SPI_SCK       GPIO_NUM_5
#define BOARD_GPIO_SPI_MISO      GPIO_NUM_3

#define BOARD_GPIO_LORA_NSS      GPIO_NUM_10
#define BOARD_GPIO_LORA_BUSY     GPIO_NUM_0
#define BOARD_GPIO_LORA_NRST     GPIO_NUM_9
#define BOARD_GPIO_LORA_DIO1     GPIO_NUM_20

/* I2C */
#define BOARD_GPIO_I2C_SDA       GPIO_NUM_7
#define BOARD_GPIO_I2C_SCL       GPIO_NUM_8

/* AS3935 wake interrupt (from sensor board) */
#define BOARD_GPIO_WAKE_IRQ      GPIO_NUM_1

/* TPS2121 power mux status */
#define BOARD_GPIO_CHARGE_SOURCE_STATUS  GPIO_NUM_6

/* PCA9536 pin assignment (main board) */
#define BOARD_PCA9536_PIN_CHRG_STATUS   0
#define BOARD_PCA9536_PIN_STDBY_STATUS  1
#define BOARD_PCA9536_PIN_J4            2

/* INA226 instances */
#define BOARD_INA226_BATTERY_ADDRESS    0x44
#define BOARD_INA226_SOLAR_ADDRESS      0x40
#define BOARD_INA226_BATTERY_SHUNT_OHM  0.020f
#define BOARD_INA226_SOLAR_SHUNT_OHM    0.020f

#define BOARD_DEVICE_ID_MAX_LEN  16   /* "meteo-a17c42" + NUL */

/**
 * Configures board GPIOs and takes ownership of the PCA9536 on the given
 * I2C bus. Must run before board_get_*().
 */
esp_err_t board_init(i2c_master_bus_handle_t i2c_bus);

/**
 * Transport selected by the J4 switch. Polarity comes from
 * CONFIG_METEO_J4_WIFI_ACTIVE_LEVEL (board-level policy).
 */
esp_err_t board_get_transport_mode(transport_mode_t *mode);

/** TP5000 charger state from CHRG_STATUS/STDBY_STATUS. */
esp_err_t board_get_charge_state(charge_state_t *state);

/**
 * Power source from CHARGE_SOURCE_STATUS_OUT. Only states the
 * hardware can actually distinguish are reported.
 */
esp_err_t board_get_power_source(power_source_t *source);

/**
 * Combined automatic USB-presence detection: TPS2121 status line (main PCB)
 * or, as a fallback, an enumerated USB host on the USB-Serial-JTAG peripheral
 * (GPIO18/19, e.g. ESP32-C3 Super Mini). No configuration needed on either
 * board; used to select continuous (no deep sleep) operation.
 */
bool board_usb_power_present(void);

/** "meteo-<last 3 MAC bytes, lowercase hex>", e.g. "meteo-a17c42". */
esp_err_t board_get_device_id(char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif
