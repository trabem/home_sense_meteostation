idf_component_register(SRCS "transport_factory.c"
                    INCLUDE_DIRS "include"
                    REQUIRES mqtt_transport lora_transport telemetry
                    PRIV_REQUIRES log esp_common)
