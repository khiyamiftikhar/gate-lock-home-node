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
#include "esp_system.h"
#include "user_output.h"
#include "user_request.h"
#include "smartconfig.h"
#include "sync_manager.h"
#include "log_capture.h"


//static const uint8_t gate_node_mac[]={0xe4,0x65,0xb8,0x1b,0x1c,0xd8};
static const uint8_t gate_node_mac[]={0xcc,0xdb,0xa7,0x49,0xee,0x14};
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



///This function handles sending log data in chunks
///It was delegated by the event handler to the task context

static void delegated_to_task_send_log(void *arg, size_t len){
    log_snapshot_t snap={0};
    char buffer[256];       //Same as the temp buffer in log_capture.c, not neat, need to have some consistent way later
    size_t bytes_read;
    esp_err_t ret=0;

    log_snapshot_take(&snap);
    uint8_t count=0;
    void* ctx= *(void**)arg;
    ESP_LOGI(TAG,"sending log data in chunks , ctc %p,", ctx);
    
    do{
        bytes_read=log_snapshot_read(&snap,buffer,sizeof(buffer)-1);
        //ESP_LOGI(TAG,"bytes read %d",bytes_read);
        if(bytes_read>0){
            buffer[bytes_read]='\0';   //null terminate
            ret=user_request_response_send_log(buffer,bytes_read,ctx);

            if(ret!=ESP_OK){
                ESP_LOGE(TAG,"failed to send log chunk");
                return;
            }
        }
        else{
            ESP_LOGI(TAG,"no more log data");
            user_request_response_send_log(NULL,0,ctx);
        }
    }while(bytes_read>0);

}



/// @brief This task runs delegated functions posted to the delegate queue.
/// @param arg 
static void delegate_run_task(void *arg) {
    delegate_job_t job;
    while (1) {
        if (xQueueReceive(delegate_queue, &job, portMAX_DELAY)) {
            job.func(job.arg_data, job.arg_len);
        }
    }
}

/// @brief The tasks that are blocking or interative cannot be run directly in event handler context.
/// This method delegates such tasks to a separate task via a queue.   
/// @param func 
/// @param arg 
/// @param len 
/// @return 
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

        //This happens on OTA
        case OTA_SERVICE_ROUTINE_EVENT_REBOOT_REQUIRED:
            wifi_set_reconnect(false);
            esp_restart();
            break;
        //This occurs on the reboot after ota, firmware still requires verification
        //If not verfied, it will be marked invalid, and skipped later on updates
        case OTA_SERVICE_ROUTINE_EVENT_VERIFICATION_PENDING:
            ota_set_valid(true);
            wifi_set_reconnect(false);
            esp_restart();
            break;



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

                    sync_manager_signal_set(SYNC_EVENT_DISCOVERY_COMPLETE);


            break;

        default:
            break;



    }

}



static void routine_user_request_events_handler (void *handler_arg,
                                    int32_t id,
                                    void *event_data){

    //a makeshift workaround to scan all the channels one by one
                                        
    esp_err_t ret=0;
    void** context=(void**)event_data;
    void* ctx=*context;

    ESP_LOGI(TAG,"ctx ptr address print %p",context);
    ESP_LOGI(TAG,"ctx address print %p",ctx);

    switch(id){

        case USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_GATE_OPEN:
                ret=message_codec_send_command(gate_node_mac ,MESSAGE_COMMAND_OPEN_LOCK,ctx);
                break;


        case USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_GATE_CLOSE:
                ret=message_codec_send_command(gate_node_mac,MESSAGE_COMMAND_CLOSE_LOCK,ctx);
                break;
        case USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_GATE_STATUS:
                ret=message_codec_send_command(gate_node_mac,MESSAGE_COMMAND_LOCK_STATUS,ctx);
                break;
        case USER_REQUEST_ROUTINE_EVENT_USER_COMMAND_LOG:

           delegate_post(delegated_to_task_send_log,&ctx,sizeof(void*));

                


        default:
            break;

    }

    //Right now responds true unconditionally
    if(ret!=ESP_OK){
        ESP_LOGI(TAG,"failure");
        user_request_response_inform_command_status(false,ctx);
    }

}




static void routine_message_service_events_handler (void *handler_arg,
                                    int32_t id,
                                    void *event_data){



    switch(id){

        case MESSAGE_SERVICE_ROUTINE_EVENT_SEND_STATUS:{
            
            message_send_ack_t* msg_send_ack=(message_send_ack_t*)event_data;
            //ESP_LOGI(TAG,"success in event handler %d",msg_send_ack->success);
            ///context=(void**)msg_send_ack->context;
            user_request_response_inform_command_status(msg_send_ack->success,msg_send_ack->context);
            break;
        }
        
        default:
            break;

    }
 }







void routine_event_handler (void *handler_arg,
                            esp_event_base_t base,
                            int32_t id,
                            void *event_data){

    

        if(base==DISCOVERY_SERVICE_ROUTINE_EVENT_BASE){
            ESP_LOGI(TAG,"routine discovery event");
            routine_discovery_events_handler(handler_arg,id,event_data);
        }
         
        else if(base==USER_REQUEST_ROUTINE_EVENT_BASE){
            //ESP_LOGI(TAG,"routine discovery event");
            routine_user_request_events_handler(handler_arg,id,event_data);
        }

        else if(base==OTA_SERVICE_ROUTINE_EVENT_BASE){
            //ESP_LOGI(TAG,"routine discovery event");
            routine_ota_service_events_handler(handler_arg,id,event_data);
        }   

         else if(base==MESSAGE_CODEC_ROUTINE_EVENT_BASE){
            //ESP_LOGI(TAG,"routine discovery event");
            routine_message_service_events_handler(handler_arg,id,event_data);
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
            4096,        // stack size
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