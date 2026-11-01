idf_component_register(SRCS "mqtt.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver mqtt json esp_timer)
