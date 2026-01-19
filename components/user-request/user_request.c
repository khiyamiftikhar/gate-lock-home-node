#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "user_request.h"
#include "http_server.h"


#define     QUEUE_SIZE          10

DEFINE_EVENT_ADAPTER(USER_REQUEST);

static const char* TAG="user request";

static struct{



    http_server_interface_t* server_interface;    //Required by it
//  QueueHandle_t response_queue;


}user_request_state={0};




static void log_request_handler(http_request_t* request,const char* uri){
    BaseType_t ret=pdTRUE;
 
    ESP_LOGI(TAG,"log handler entered");
    //Keep reading from queue until it is free from previous entries
    //bool success;
    //while(ret==pdTRUE){
     //   ret=get_from_queue(0,&success);      //0 wait time
   /// }
    
    //ESP_LOGI(TAG,"gate close proceed");
    //Now send new command. The data that will arrive in queue now belongs to this request
    esp_err_t err=0;
    err=USER_REQUEST_post_event(USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_LOG,&request,sizeof(request));

    if (err != ESP_OK) {
        user_request_state.server_interface->send_response(request,"failure");
        user_request_state.server_interface->close_async_connection(request);
        return;
    }

    //return ESP_OK;

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
    err=USER_REQUEST_post_event(USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_GATE_CLOSE,&request,sizeof(request));

    if (err != ESP_OK) {
        user_request_state.server_interface->send_response(request,"failure");
        user_request_state.server_interface->close_async_connection(request);
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
    err=USER_REQUEST_post_event(USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_GATE_OPEN,&request,sizeof(request));

    if (err != ESP_OK) {
        user_request_state.server_interface->send_response(request,"failure");
        user_request_state.server_interface->close_async_connection(request);
        return;
    }

    //return ESP_OK;
    
 //   return ESP_OK;
    
    
}



esp_err_t user_request_create(user_request_config_t* config){
    
    esp_err_t ret=0;
    if(config==NULL || config->gate_close_endpoint==NULL || config->gate_close_endpoint==NULL){
        return ESP_FAIL;
    }

    
    
    if (http_server_get_interface() == NULL) {
        http_server_config_t server_config=HTTP_SERVER_DEFAULT_CONFIG();
        ret=http_server_init(&server_config);
        if(ret!=ESP_OK){
            ESP_LOGE(TAG,"HTTP server init failed");
            return ret;
        }
   
    }
    
    user_request_state.server_interface=http_server_get_interface();

    
    user_request_state.server_interface->register_uri(config->gate_close_endpoint,METHOD_GET,gate_close_request_handler);
    user_request_state.server_interface->register_uri(config->gate_open_endpoint,METHOD_GET,gate_open_request_handler);
    user_request_state.server_interface->register_uri(config->log_endpoint,METHOD_GET,log_request_handler);


    //The response of esp send will be pushed to this queue by the method of output interface
    //user_request_state.response_queue=xQueueCreate(QUEUE_SIZE,sizeof(bool));

    USER_REQUEST_register_event(USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_GATE_OPEN,NULL,NULL);
    USER_REQUEST_register_event(USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_GATE_CLOSE,NULL,NULL);
    USER_REQUEST_register_event(USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_GATE_STATUS,NULL,NULL);
    USER_REQUEST_register_event(USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_LOG,NULL,NULL);
     


    //ESP_ERROR_CHECK(user_request_state.response_queue==NULL);

   // user_request_state.server_interface->register_uri(config->gate_open_endpoint,METHOD_GET,gate_close_request_handler);
    return ESP_OK;

}

