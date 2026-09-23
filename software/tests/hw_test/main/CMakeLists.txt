idf_component_register(SRCS "hw_test_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES as3935 bme280 board ds3231 i2c_bus ina226 pca9536
                             sx1262 telemetry power_manager app_flow
                    PRIV_REQUIRES esp_timer)
