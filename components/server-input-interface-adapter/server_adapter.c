#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "server_adapter.h"
#include "relay_server.h"


#define     QUEUE_SIZE          10

static const char* TAG="user interaction";

static struct{

    user_interaction_interface_t adapter_interface; //It provides it
    relay_server_interface_t* server_interface;    //Required by it
    user_command_callback command_handler;      //Required by it
    QueueHandle_t response_queue;


}user_interaction={0};

static esp_err_t inform_lock_status(lock_status_t status){

    ESP_LOGI(TAG,"not yet implemented");
    return ESP_OK;
}
    //When command is succesfully sent
static esp_err_t inform_command_status(bool success){  
    ESP_LOGI(TAG,"push to queue %d",success);
    xQueueSend(user_interaction.response_queue,&success,portMAX_DELAY);
    return ESP_OK;

}

static bool get_from_queue(){
    bool success;
    xQueueReceive(user_interaction.response_queue,&success,portMAX_DELAY);
    ESP_LOGI(TAG,"received from queue %d",success);
    return success;
}

static void gate_close_request_handler(http_request_t* request,const char* uri){
    esp_err_t ret=0;
 
    ret=user_interaction.command_handler(USER_COMMAND_LOCK_CLOSE);
    
    bool success=get_from_queue();      //blocking call untill the espnow send callback pushes to queue

    if(success)
        user_interaction.server_interface->send_response(request,"Gate Closed");
    else
        user_interaction.server_interface->send_response(request,"Failed");
    
    
    //return ESP_OK;

}

static void gate_open_request_handler(http_request_t* request,const char* uri){
    
    
    esp_err_t ret=user_interaction.command_handler(USER_COMMAND_LOCK_OPEN);

    
    bool success=get_from_queue();      //blocking call untill the espnow send callback pushes to queue

    if(success)
        user_interaction.server_interface->send_response(request,"Gate Opened");
    else
        user_interaction.server_interface->send_response(request,"Failed");
    
    
    
 //   return ESP_OK;
    
    
}


static esp_err_t register_user_command_callback(user_command_callback cb){
    if(cb==NULL)
        return ESP_ERR_INVALID_ARG;
    user_interaction.command_handler=cb;
    return ESP_OK;
}



user_interaction_interface_t* user_interaction_create(user_interaction_config_t* config){
    if(config==NULL || config->gate_close_endpoint==NULL || config->gate_close_endpoint==NULL){
        return NULL;
    }

    user_interaction.command_handler=config->handler;
    relay_server_config_t server_config;
    server_config.max_connections=6;
    server_config.max_uris=3;
    server_config.port=80;
    server_config.protocol=PROTOCOL_HTTP;       //unused
    user_interaction.server_interface=relay_server_init(&server_config);

    if(user_interaction.server_interface==NULL)
        return NULL;
    user_interaction.server_interface->register_uri(config->gate_close_endpoint,METHOD_GET,gate_close_request_handler);
    user_interaction.server_interface->register_uri(config->gate_open_endpoint,METHOD_GET,gate_open_request_handler);
    user_interaction.adapter_interface.user_output.inform_command_status=inform_command_status;
    user_interaction.adapter_interface.user_output.inform_lock_status=inform_lock_status;
    user_interaction.adapter_interface.register_user_command_callback=register_user_command_callback;


    //The response of esp send will be pushed to this queue by the method of output interface
    user_interaction.response_queue=xQueueCreate(QUEUE_SIZE,sizeof(bool));


    ESP_ERROR_CHECK(user_interaction.response_queue==NULL);

   // user_interaction.server_interface->register_uri(config->gate_open_endpoint,METHOD_GET,gate_close_request_handler);
    return &user_interaction.adapter_interface;

}

