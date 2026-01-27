#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "stdbool.h"
#include "ui_home.h"
#include "gui_op.h"



#define GUI_WAIT_TIME   50
static const char* TAG = "gpu op";



typedef struct{
    gui_event_t event;
    gui_event_data_t evt_data;

}gui_op_info_t;


static struct
{
    TaskHandle_t gui_op_task;
    QueueHandle_t gui_op_queue;
    bool init;
    gui_interface_t interface;
}gui_op={0};



gui_interface_t* gui_op_get_interface(){
    if(gui_op.init==true)
        return &gui_op.interface;
    else
        return NULL;
}



static void gui_op_task(){

    gui_op_info_t op_info={0};




    while(1){


        if(xQueueReceive(gui_op.gui_op_queue,&op_info,portMAX_DELAY)==pdTRUE){

            switch(op_info.event){
    
                case SYSTEM_BOOTING:
                    ui_home_load_screen();
                    ui_home_screen_set_main_label("booting");
                    
                    break;

                case SYSTEM_WIFI_AP_SCANNING:

                    ui_home_screen_set_main_label("scanning wifi...");

                    
                    
                    
                    ui_home_screen_set_wifi_ssid("some_ap");
                    
                    break;

                case SYSTEM_WIFI_STA_CONNECTED:
                    
                    ui_home_screen_set_main_label("Wifi connected");
                    
                    ui_home_screen_set_wifi_ssid("some_ap");
                    break;

                case SYSTEM_ESPNOW_STARTED:
                    
                    ui_home_screen_set_main_label("esp_now started");
                    
                    break;

                case SYSTEM_USER_COMMAND_RECEIVED:
                    ui_home_screen_set_main_label("command received");
                    
                    break;

                   
                default:
                    break;

           }
        }



    }



}


esp_err_t gui_inform(gui_event_t event, gui_event_data_t *evt_data)
{
    
    
    ESP_LOGI(TAG,"updataing gui");
    gui_op_info_t op_info = {0};

    op_info.event = event;

    if (evt_data)
        memcpy(&op_info.evt_data,evt_data,sizeof(gui_event_data_t));
        //op_info.evt_data = *evt_data;   // full struct copy

    BaseType_t ret = xQueueSend(
        gui_op.gui_op_queue,
        &op_info,
        pdMS_TO_TICKS(GUI_WAIT_TIME)
    );

    return (ret == pdTRUE) ? ESP_OK : ESP_FAIL;
}

esp_err_t gui_inform_dummy(gui_event_t event, gui_event_data_t *evt_data){
    return ESP_OK;
}

esp_err_t gui_op_init(){

    #if !CONFIG_FEATURE_GUI

        gui_op.interface.gui_inform=gui_inform_dummy;
        gui_op.init=true;

        return ESP_OK;
    #endif
    gui_op.gui_op_queue = xQueueCreate(10, sizeof(gui_op_info_t));
    ESP_ERROR_CHECK(gui_op.gui_op_queue==NULL);

    BaseType_t  ret=xTaskCreate(gui_op_task, "lvgl_notify", 2048, NULL, 5, &gui_op.gui_op_task);
    ESP_ERROR_CHECK(ret!=pdTRUE);

    gui_op.interface.gui_inform=gui_inform;
    gui_op.init=true;

    return ESP_OK;

}

