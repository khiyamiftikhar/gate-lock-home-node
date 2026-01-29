set(GUI_SRCS
    src/gui_op_dummy.c
    src/lcd_device_dummy.c
)

if(CONFIG_FEATURE_GUI)
    list(APPEND GUI_SRCS
        src/gui_op.c
        src/lcd_device.c
        priv_src/home_screen.c
        priv_src/ui_worker.c
    )
endif()

idf_component_register(
    SRCS ${GUI_SRCS}
    INCLUDE_DIRS include .
    PRIV_INCLUDE_DIRS priv_include
    REQUIRES gui-interface
    PRIV_REQUIRES
        esp_timer
        lvgl
        esp_lvgl_port
)
