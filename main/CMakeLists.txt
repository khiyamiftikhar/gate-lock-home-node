idf_component_register(SRCS "home-node.c" "routine_event_handler.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp-now-comm peer-registry discovery_service message_service 
                                    peer-registry discovery_service esp_wifi wpa_supplicant 
                                    nvs_flash wifi-smartconfig user-request user-request-response
                                    ota-service mdns-service
                                    sync-manager
                                    gui-interface gui-component log-capture
                                    )
