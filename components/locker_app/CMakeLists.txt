idf_component_register(SRCS "locker_app.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "."
                    PRIV_REQUIRES gm65_uart weight_sensor)
