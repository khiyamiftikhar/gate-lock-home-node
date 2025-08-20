#include <stdio.h>
#include "esp_log.h"

#include "user_output.h"


static const char* TAG="user output";


static struct{

    
    user_output_interface_t interface;
    


}user_output_state={0};





static esp_err_t inform_lock_status(lock_status_t status){
    ESP_LOGI(TAG,"Lock status not implemented");
    return 0;
}
    //When command is succesfully sent
static esp_err_t inform_command_status(bool success){
    ESP_LOGI(TAG,"command status not implemented");
    return 0;

}









user_output_interface_t* user_output_create(){

    user_output_state.interface.inform_command_status=inform_command_status;
    user_output_state.interface.inform_lock_status=inform_lock_status;
    
    return &user_output_state.interface;
}

