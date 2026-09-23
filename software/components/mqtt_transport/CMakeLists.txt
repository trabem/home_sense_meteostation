idf_component_register(SRCS "mqtt_transport.c"
                    INCLUDE_DIRS "include"
                    REQUIRES transport app_events espressif__mqtt mbedtls
                    PRIV_REQUIRES esp_event freertos log esp_common)
