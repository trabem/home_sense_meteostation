idf_component_register(SRCS "lora_transport.c"
                    INCLUDE_DIRS "include"
                    REQUIRES transport sx1262 telemetry log esp_common)
