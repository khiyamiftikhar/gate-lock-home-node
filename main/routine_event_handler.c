#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "event_system_adapter.h"
#include "routine_event_handler.h"
#include "espnow_discovery.h"
#include "ota_service.h"
#include "esp_now_transport.h"
#include "message_codec.h"

#include "server_adapter.h"


static const uint8_t gate_node_mac[]={0xe4,0x65,0xb8,0x1b,0x1c,0xd8};
static const char* TAG="Routine";

#define     MAX_WIFI_CHANNEL        13
#define     DELEGATE_QUEUE_LENGTH   4

#define DELEGATE_ARG_MAX_SIZE 32   // tune as needed; small fixed buffer

typedef void (*delegate_func_t)(void *arg, size_t len);

typedef struct {
    delegate_func_t func;
    uint8_t arg_data[DELEGATE_ARG_MAX_SIZE];
    size_t arg_len;
} delegate_job_t;

static QueueHandle_t delegate_queue = NULL;


static TaskHandle_t delegate_task_handle=NULL;





static void delegate_run_task(void *arg) {
    delegate_job_t job;
    while (1) {
        if (xQueueReceive(delegate_queue, &job, portMAX_DELAY)) {
            job.func(job.arg_data, job.arg_len);
        }
    }
}


static esp_err_t delegate_post(delegate_func_t func, const void *arg, size_t len) {
    if (len > DELEGATE_ARG_MAX_SIZE) {
        ESP_LOGE("DELEGATE", "Argument too large (%d > %d)", len, DELEGATE_ARG_MAX_SIZE);
        return ESP_ERR_INVALID_ARG;
    }

    delegate_job_t job = { .func = func, .arg_len = len };
    if (arg && len > 0) {
        memcpy(job.arg_data, arg, len);
    }

    if (xQueueSend(delegate_queue, &job, portMAX_DELAY) != pdPASS) {
        ESP_LOGE("DELEGATE", "Failed to post delegate job");
        return ESP_FAIL;
    }
    return ESP_OK;
}





static void routine_ota_service_events_handler(void *handler_arg,
                                    int32_t id,
                                    void *event_data){
    switch(id){

        
        default:
            break;
    }


                                            


                                    }



static void routine_discovery_events_handler (void *handler_arg,
                                    int32_t id,
                                    void *event_data){

    //a makeshift workaround to scan all the channels one by one
    static uint8_t channel=0;
    
    switch(id){

        case DISCOVERY_EVENT_DISCOVERY_COMPLETE:
            break;

        default:
            break;



    }

}



static void routine_server_adapter_events_handler (void *handler_arg,
                                    int32_t id,
                                    void *event_data){

    //a makeshift workaround to scan all the channels one by one
                                        
    
    switch(id){

        case SERVER_ADAPTER_ROUTINE_EVENT_USER_COMMAND_GATE_OPEN:
                message_codec_send_command(gate_node_mac ,MESSAGE_COMMAND_OPEN_LOCK);
                break;


        case SERVER_ADAPTER_ROUTINE_EVENT_USER_COMMAND_GATE_CLOSE:
                message_codec_send_command(gate_node_mac,MESSAGE_COMMAND_CLOSE_LOCK);
                break;
        case SERVER_ADAPTER_ROUTINE_EVENT_USER_COMMAND_GATE_STATUS:
                message_codec_send_command(gate_node_mac,MESSAGE_COMMAND_LOCK_STATUS);
                break;

        default:
            break;

    }

    //Right now responds true unconditionally
    user_interaction_inform_command_status(true,event_data);

}



/*
static void routine_espnow_transport_events_handler (void *handler_arg,
                                    int32_t id,
                                    void *event_data){


    switch(id){

        case ESPNOW_TRANSPORT_ROUTINE_EVENT_DISCOVERY_INCOMING:{
            uint8_t* src_mac=(uint8_t*)event_data;
            ESP_LOGI(TAG,"discovery incoming");
            discovery_events_handler(DISCOVERY_EVENT_DISCOVERY_MESSAGE_ARRIVED,src_mac);
            break;
        }
        case ESPNOW_TRANSPORT_ROUTINE_EVENT_DISCOVERY_ACK_INCOMING:{
            uint8_t* src_mac=(uint8_t*)event_data;
            discovery_events_handler(DISCOVERY_EVENT_DISCOVERY_MESSAGE_ACK_ARRIVED,src_mac);
            break;
        }
        

        case ESPNOW_TRANSPORT_ROUTINE_EVENT_MSG_SENT:{
            espnow_msg_sent_status_t* msg=(espnow_msg_sent_status_t*)event_data;
            ESP_LOGI(TAG,"success %d",msg->success);


            break;
        }

        default:
            break;

    }
 }
*/






void routine_event_handler (void *handler_arg,
                            esp_event_base_t base,
                            int32_t id,
                            void *event_data){

    

        if(base==DISCOVERY_SERVICE_ROUTINE_EVENT_BASE){
            ESP_LOGI(TAG,"routine discovery event");
            routine_discovery_events_handler(handler_arg,id,event_data);
        }
         
        else if(base==SERVER_ADAPTER_ROUTINE_EVENT_BASE){
            //ESP_LOGI(TAG,"routine discovery event");
            routine_server_adapter_events_handler(handler_arg,id,event_data);
        }

        else if(base==OTA_SERVICE_ROUTINE_EVENT_BASE){
            //ESP_LOGI(TAG,"routine discovery event");
            routine_ota_service_events_handler(handler_arg,id,event_data);
        }   

    
        

        

}



esp_err_t routine_handler_init(){

    if (delegate_queue == NULL) {
        delegate_queue = xQueueCreate(DELEGATE_QUEUE_LENGTH, sizeof(delegate_job_t));
        if (delegate_queue == NULL) {
            ESP_LOGE(TAG, "failed to create delegate queue");
            return ESP_FAIL;
        }
    }

    if (delegate_task_handle == NULL) {
        BaseType_t res = xTaskCreatePinnedToCore(
            delegate_run_task,
            "run delegated tasks",
            3072,          // stack size
            NULL,
            5,             // priority
            &delegate_task_handle,
            tskNO_AFFINITY // can pin to core 0 or 1 if you prefer
        );
        if (res != pdPASS) {
            ESP_LOGE(TAG, "failed to create delegate task");
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}