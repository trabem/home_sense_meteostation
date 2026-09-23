idf_component_register(SRCS "app_storage.c"
                    INCLUDE_DIRS "include"
                    REQUIRES nvs_flash esp_common
                    PRIV_REQUIRES log)
