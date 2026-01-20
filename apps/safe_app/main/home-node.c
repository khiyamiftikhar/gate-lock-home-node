
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "smartconfig.h"
#include "ota_service.h"
#include "event_system_adapter.h"
#include "routine_event_handler.h"
#include "sync_manager.h"



//#include "mdns_service.h"

static const char* TAG="main gate";

static bool proceed=false;




static void esp_flash_init(){
     esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

}


void app_main(void)
{
    
    esp_err_t ret=0;
    //esp_log_level_set("ESP_NOW_TRANSPORT", ESP_LOG_NONE);
    
    esp_flash_init();
    
    sync_manager_init();
    event_system_adapter_init(routine_event_handler,NULL);

    routine_handler_init();

    

    


    wifi_smartconfig_t wifi_cfg={.callback=NULL, .power_save=false};

    wifi_initialize(&wifi_cfg);
    
    
    
    
    
    
    

    ESP_LOGI("MEM", "Free heap: %u",
         (unsigned int) esp_get_free_heap_size());

    ESP_LOGI("MEM", "Min free heap: %u",
         (unsigned int) esp_get_minimum_free_heap_size());

        
    


    esp_err_t ota_err=ota_service_init();

    ota_process_start();

    while(1){
        //user_command(USER_COMMAND_LOCK_CLOSE);
        vTaskDelay(pdMS_TO_TICKS(1000));
        //user_command(USER_COMMAND_LOCK_OPEN);
        //vTaskDelay(pdMS_TO_TICKS(500));
    }
    


}
