#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "server_adapter.h"
#include "relay_server.h"


#define     QUEUE_SIZE          10

DEFINE_EVENT_ADAPTER(SERVER_ADAPTER);

static const char* TAG="user interaction";

static struct{

    
    relay_server_interface_t* server_interface;    //Required by it
    QueueHandle_t response_queue;


}user_interaction={0};

/*
static esp_err_t inform_lock_status(lock_status_t status){

    ESP_LOGI(TAG,"not yet implemented");
    return ESP_OK;
}*/
    //When command is succesfully sent
esp_err_t user_interaction_inform_command_status(bool success,void* context){  
    
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

static BaseType_t get_from_queue(TickType_t wait_time,bool* success){
    
    BaseType_t ret=xQueueReceive(user_interaction.response_queue,success,wait_time);
    ESP_LOGI(TAG,"received from queue %d",*success);
    return ret;
}




static void gate_close_request_handler(http_request_t* request,const char* uri){
    BaseType_t ret=pdTRUE;
 
    ESP_LOGI(TAG,"gate close handler entered");
    //Keep reading from queue until it is free from previous entries
    //bool success;
    //while(ret==pdTRUE){
     //   ret=get_from_queue(0,&success);      //0 wait time
   /// }
    
    //ESP_LOGI(TAG,"gate close proceed");
    //Now send new command. The data that will arrive in queue now belongs to this request
    esp_err_t err=0;
    err=SERVER_ADAPTER_post_event(SERVER_ADAPTER_ROUTINE_EVENT_USER_COMMAND_GATE_CLOSE,&request,sizeof(request));

    if (err != ESP_OK) {
        user_interaction.server_interface->send_response(request,"failure");
        user_interaction.server_interface->close_async_connection(request);
        return;
    }

    //return ESP_OK;

}

static void gate_open_request_handler(http_request_t* request,const char* uri){
    
    
    BaseType_t ret=pdTRUE;
    ESP_LOGI(TAG,"gate open handler entered");
    //Keep reading from queue until it is free from previous entries
    //bool success;
    //while(ret==pdTRUE){
     //   ret=get_from_queue(0,&success);      //0 wait time
   /// }
    
    //ESP_LOGI(TAG,"gate open proceed");
    //Now send new command. The data that will arrive in queue now belongs to this request
    esp_err_t err=0;
    ESP_LOGI(TAG,"req address print %p",(void*)request);
    ESP_LOGI(TAG,"req ptr address print %p",(void*)&request);
    err=SERVER_ADAPTER_post_event(SERVER_ADAPTER_ROUTINE_EVENT_USER_COMMAND_GATE_OPEN ,&request,sizeof(request));

    if (err != ESP_OK) {
        user_interaction.server_interface->send_response(request,"failure");
        user_interaction.server_interface->close_async_connection(request);
        return;
    }

    //return ESP_OK;
    
 //   return ESP_OK;
    
    
}



esp_err_t user_interaction_create(user_interaction_config_t* config){
    
    
    if(config==NULL || config->gate_close_endpoint==NULL || config->gate_close_endpoint==NULL){
        return ESP_FAIL;
    }

    
    relay_server_config_t server_config;
    server_config.max_connections=6;
    server_config.max_uris=3;
    server_config.port=80;
    server_config.protocol=PROTOCOL_HTTP;       //unused
    user_interaction.server_interface=relay_server_init(&server_config);

    if(user_interaction.server_interface==NULL)
        return ESP_FAIL;
    user_interaction.server_interface->register_uri(config->gate_close_endpoint,METHOD_GET,gate_close_request_handler);
    user_interaction.server_interface->register_uri(config->gate_open_endpoint,METHOD_GET,gate_open_request_handler);


    //The response of esp send will be pushed to this queue by the method of output interface
    user_interaction.response_queue=xQueueCreate(QUEUE_SIZE,sizeof(bool));

    SERVER_ADAPTER_register_event(SERVER_ADAPTER_ROUTINE_EVENT_USER_COMMAND_GATE_OPEN,NULL,NULL);
    SERVER_ADAPTER_register_event(SERVER_ADAPTER_ROUTINE_EVENT_USER_COMMAND_GATE_CLOSE,NULL,NULL);
    SERVER_ADAPTER_register_event(SERVER_ADAPTER_ROUTINE_EVENT_USER_COMMAND_GATE_STATUS,NULL,NULL);

     


    ESP_ERROR_CHECK(user_interaction.response_queue==NULL);

   // user_interaction.server_interface->register_uri(config->gate_open_endpoint,METHOD_GET,gate_close_request_handler);
    return ESP_OK;

}

