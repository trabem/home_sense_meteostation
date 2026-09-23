idf_component_register(SRCS "sensor_manager.c"
                    INCLUDE_DIRS "include"
                    REQUIRES telemetry bme280 ina226 board diagnostics esp_common
                    PRIV_REQUIRES log)
