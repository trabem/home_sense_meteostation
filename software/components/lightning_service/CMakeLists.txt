idf_component_register(SRCS "lightning_service.c"
                    INCLUDE_DIRS "include"
                    REQUIRES as3935 telemetry esp_common
                    PRIV_REQUIRES log)
