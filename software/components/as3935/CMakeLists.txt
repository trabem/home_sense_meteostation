idf_component_register(SRCS "as3935.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_i2c esp_common
                    PRIV_REQUIRES log freertos)
