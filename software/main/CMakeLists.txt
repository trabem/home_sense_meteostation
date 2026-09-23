idf_component_register(SRCS "app_main.c" "app.c" "app_config.c"
                    INCLUDE_DIRS "."
                    REQUIRES app_events app_flow app_storage as3935 bme280 board
                             diagnostics ds3231 i2c_bus ina226 lightning_service
                             power_manager sensor_manager sx1262 telemetry
                             transport lora_transport mqtt_transport wifi_manager
                             wifi_provisioning
                    PRIV_REQUIRES esp_app_format esp_netif esp_timer nvs_flash)
