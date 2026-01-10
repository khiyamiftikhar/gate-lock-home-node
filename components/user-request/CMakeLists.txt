idf_component_register(SRCS "user_request.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES http-server 
                    REQUIRES event-adapter)