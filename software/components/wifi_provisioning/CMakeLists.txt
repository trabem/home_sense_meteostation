idf_component_register(SRCS "wifi_provisioning.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi esp_event espressif__network_provisioning
                    PRIV_REQUIRES esp_hw_support freertos log esp_common)
