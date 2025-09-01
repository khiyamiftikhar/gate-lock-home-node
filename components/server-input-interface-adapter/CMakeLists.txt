idf_component_register(SRCS server_adapter.c
                        INCLUDE_DIRS .
                        REQUIRES message_service 
                        PRIV_REQUIRES http-server
                        )