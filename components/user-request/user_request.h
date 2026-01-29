#ifndef USER_REQUEST_H
#define USER_REQUEST_H



#include "event_system_adapter.h"
#include "stdint.h"

DECLARE_EVENT_ADAPTER(USER_REQUEST);

#define USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_GATE_OPEN     1
#define USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_GATE_CLOSE    2
#define USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_GATE_STATUS   3
#define USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_LOG           4
#define USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_OTA_UPDATE    5


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
    const char* log_endpoint;
    const char* ota_update_endpoint;

}user_request_config_t;


/// @brief Inform whether request was sent successfully using espnow
/// @param success 
/// @return 

esp_err_t user_request_create(user_request_config_t* config);
#endif
