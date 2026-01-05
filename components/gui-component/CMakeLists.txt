idf_component_register( SRCS "src/gui_op.c" "src/lcd_device.c"
                            "priv_src/home_screen.c" "priv_src/ui_worker.c" 
                        INCLUDE_DIRS "include" "."
                        PRIV_INCLUDE_DIRS "priv_include"
                        PRIV_REQUIRES lvgl esp_lvgl_port esp_timer
                        REQUIRES gui-interface
                        )


add_compile_definitions(ESP_PLATFORM)