idf_component_register(SRCS "app_events.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_event
                    PRIV_REQUIRES log esp_common)
