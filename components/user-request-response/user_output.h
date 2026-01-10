#ifndef USER_OUTPUT_H
#define USER_OUTPUT_H




#include "stdint.h"
#include "esp_err.h"


/// @brief Inform whether request was sent successfully using espnow
/// @param success 
/// @return 
esp_err_t user_request_response_inform_command_status(bool success,void* context);
esp_err_t user_request_response_create();

#endif
