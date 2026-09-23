idf_component_register(SRCS "board.c"
                    INCLUDE_DIRS "include"
                    REQUIRES telemetry app_flow pca9536 esp_driver_i2c esp_driver_gpio
                    PRIV_REQUIRES esp_hw_support log esp_common)
