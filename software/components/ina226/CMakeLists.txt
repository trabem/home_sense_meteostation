idf_component_register(SRCS "ina226.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_i2c esp_common telemetry
                    PRIV_REQUIRES log freertos)
