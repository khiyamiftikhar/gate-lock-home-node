idf_component_register(SRCS user_output.c
                        INCLUDE_DIRS .
                        REQUIRES event-adapter
                        PRIV_REQUIRES http-server
                        )