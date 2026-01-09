idf_component_register(SRCS server_adapter.c
                        INCLUDE_DIRS .
                        REQUIRES message_service 
                        REQUIRES event-adapter
                        PRIV_REQUIRES http-server
                        )