idf_component_register(SRCS "power_manager.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_flow esp_driver_gpio esp_hw_support esp_common
                    PRIV_REQUIRES log)
