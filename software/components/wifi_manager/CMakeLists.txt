idf_component_register(SRCS "wifi_manager.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi esp_event esp_netif app_events
                    PRIV_REQUIRES freertos log esp_common)
