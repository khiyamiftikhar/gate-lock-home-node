idf_component_register(SRCS ota_service.c
                        INCLUDE_DIRS .
                        PRIV_REQUIRES esp_http_client app_update json
                        EMBED_TXTFILES cert/ca_cert.pem)