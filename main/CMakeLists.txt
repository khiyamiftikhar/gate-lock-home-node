idf_component_register(SRCS "home-node.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp-now-comm peer-registry discovery_service message_service esp_wifi wpa_supplicant nvs_flash user_output wifi-smartconfig server-input-interface-adapter ota-service)
