#ifndef SERVER_ADAPTER_H
#define SERVER_ADAPTER_H



#include "event_system_adapter.h"
#include "stdint.h"

DECLARE_EVENT_ADAPTER(SERVER_ADAPTER);

#define SERVER_ADAPTER_ROUTINE_EVENT_USER_COMMAND_GATE_OPEN     1
#define SERVER_ADAPTER_ROUTINE_EVENT_USER_COMMAND_GATE_CLOSE    2
#define SERVER_ADAPTER_ROUTINE_EVENT_USER_COMMAND_GATE_STATUS   3


//This is the interface it provides
/*
typedef struct {
    esp_err_t (*register_user_command_callback)(user_command_callback cb);
    user_output_interface_t user_output;
    
}user_interaction_interface_t;
*/

typedef struct{

    const char* gate_open_endpoint;
    const char* gate_close_endpoint;
}user_interaction_config_t;


/// @brief Inform whether request was sent successfully using espnow
/// @param success 
/// @return 
esp_err_t user_interaction_inform_command_status(bool success,void* context);
esp_err_t user_interaction_create(user_interaction_config_t* config);

#endif