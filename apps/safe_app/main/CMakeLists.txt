idf_component_register(SRCS "home-node.c" "routine_event_handler.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES   esp_wifi wpa_supplicant 
                                    nvs_flash wifi-smartconfig
                                    ota-service 
                                    sync-manager
                                    
                                    )
