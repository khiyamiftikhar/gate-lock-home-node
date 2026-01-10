#ifndef USER_BUTTONS_H
#define USER_BUTTONS_H


#include "home_node.h"
#include "stdint.h"





typedef struct {
    esp_err_t (*register_callback)(user_command_callback cb);
    
}user_input_interface_t;

typedef struct{

    uint8_t open_button_gpio_no;
    uint8_t close_button_gpio_no;
    //user_command_callback callback;    
}user_input_config_t;




user_input_interface_t* user_input_create(user_input_config_t* config);

#endif