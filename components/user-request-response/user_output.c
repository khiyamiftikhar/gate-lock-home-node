#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
//#include "freertos/FreeRTOS.h"
//#include "freertos/task.h"
//#include "freertos/queue.h"
#include "http_server.h"
#include "user_output.h"



static const char* TAG="user response";

static struct{

    
   http_server_interface_t* server_interface;    //Required by it
    

}user_interaction={0};

/*
static esp_err_t inform_lock_status(lock_status_t status){

    ESP_LOGI(TAG,"not yet implemented");
    return ESP_OK;
}*/
    //When command is succesfully sent
esp_err_t user_request_response_send_log(char* log_data,size_t length,void* context){
    
    //Send response
    http_request_t* req=(http_request_t*)context;

//    if(log_data==NULL || length==0){
  //      user_interaction.server_interface->close_async_connection(req);
    //    return ESP_FAIL;
    //}

    return user_interaction.server_interface->send_chunked_response(req,log_data);
    //Close connection
        

    //return ESP_OK;
}   

esp_err_t user_request_response_inform_command_status(bool success,void* context){  
    
    http_request_t* req=(http_request_t*)context;
    
    ESP_LOGI(TAG,"sending resp %d",success);

    if(success){

        user_interaction.server_interface->send_response(req, "success");
    }
    else{
        user_interaction.server_interface->send_response(req,"Failed");
    }
    
    user_interaction.server_interface->close_async_connection(req);
    
    return ESP_OK;

}


esp_err_t user_request_response_create(){
    
    
    //If server not already created, then create it
    esp_err_t ret=0;
    if (http_server_get_interface() == NULL) {
        http_server_config_t server_config=HTTP_SERVER_DEFAULT_CONFIG();
        ret=http_server_init(&server_config);
        if(ret!=ESP_OK){
            ESP_LOGE(TAG,"HTTP server init failed");
            return ret;
        }
    }

    user_interaction.server_interface=http_server_get_interface();

   // user_interaction.server_interface->register_uri(config->gate_open_endpoint,METHOD_GET,gate_close_request_handler);
    return ESP_OK;

}

