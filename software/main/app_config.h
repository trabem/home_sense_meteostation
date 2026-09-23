#pragma once

#include "mqtt_transport.h"
#include "power_manager.h"
#include "sx1262.h"

/** Builds the SX1262 config from board pins + Kconfig radio parameters. */
void app_config_fill_radio(sx1262_config_t *config);

/** Builds the MQTT config from Kconfig; device_id must outlive the config. */
void app_config_fill_mqtt(mqtt_transport_config_t *config, const char *device_id);

/** Builds the power manager config from board pins + Kconfig interval. */
void app_config_fill_power(power_manager_config_t *config);

/** Firmware version string from esp_app_get_description(). */
const char *app_config_fw_version(void);
