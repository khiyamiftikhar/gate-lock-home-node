#ifndef SERVER_ADAPTER_H
#define SERVER_ADAPTER_H


#include "home_node.h"
#include "stdint.h"





//This is the interface it provides
typedef struct {
    esp_err_t (*register_user_command_callback)(user_command_callback cb);
    user_output_interface_t user_output;
    
}user_interaction_interface_t;


typedef struct{

    const char* gate_open_endpoint;
    const char* gate_close_endpoint;
    user_command_callback handler;    
}user_interaction_config_t;




user_interaction_interface_t* user_interaction_create(user_interaction_config_t* config);

#endif