
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
#include "esp_now_transport.h"
#include "espnow_discovery.h"
#include "database_interface.h"
#include "discovery_timer.h"
#include "peer_registry.h"
#include "message_codec.h"
#include "mdns_service.h"
#include "smartconfig.h"
#include "server_adapter.h"
#include "ota_service.h"
#include "event_system_adapter.h"
#include "routine_event_handler.h"
//#include "mdns_service.h"

#define     ESPNOW_CHANNEL          1
#define     DISCOVERY_DURATION      15000    //ms
#define     DISCOVERY_INTERVAL      2000    //ms
#define     ESPNOW_ENABLE_LONG_RANGE    1
static const char* TAG="main gate";

static const uint8_t gate_node_mac[]={0xe4,0x65,0xb8,0x1b,0x1c,0xd8};

static bool proceed=false;

#define GATE_NODE_ID        2


static esp_err_t get_gate_node_mac(uint8_t* mac){

    //uint8_t gate_mac[]={1,2,3,4,5,6};
    memcpy(mac,&gate_node_mac,sizeof(gate_node_mac));
    return 0;

}
/*
static esp_err_t inform_command_status(bool success){
    ESP_LOGI(TAG,"success %d",success);
    return 0;

}

static esp_err_t inform_lock_status(lock_status_t status){
    ESP_LOGI(TAG,"status %d",status);
    return 0;

}


static void user_command_simulation(user_command_t command){

    
}*/

/* WiFi should start before using ESPNOW */
/*
static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start());
    ESP_ERROR_CHECK( esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    uint8_t mac[6];
    esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
    ESP_LOGI(TAG, "Device Mac is " MACSTR, MAC2STR(mac));

#if ESPNOW_ENABLE_LONG_RANGE
    ESP_ERROR_CHECK( esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );
#endif
}*/

static void esp_flash_init(){
     esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

}

void init_espnow(){
    
    proceed=true;
}

void app_main(void)
{
    
    esp_err_t ret=0;
    //esp_log_level_set("ESP_NOW_TRANSPORT", ESP_LOG_NONE);
    esp_flash_init();
    event_system_adapter_init(routine_event_handler,NULL);
    //wifi_init();
    wifi_smartconfig_t wifi_cfg={.callback=init_espnow, .power_save=false};

    wifi_initialize(&wifi_cfg);
    
    //Wait until wifi connection is established
        while(proceed==false){
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ret=mdns_service_start();
    //1esp_err_t ret=mdns_init_service();    
    esp_err_t ota_err=ota_service_init();
    uint8_t primary;
    wifi_second_chan_t second;
    ESP_ERROR_CHECK(esp_wifi_get_channel(&primary, &second));
    
    esp_now_transport_config_t transport_config={.wifi_channel=primary};

    ESP_LOGI(TAG,"primary channel %d",primary);
    
    
    //The objcts created but callbacks not assigned. will be assigned later
    ret=esp_now_transport_init(&transport_config);

    if(ret==ESP_FAIL){
        ESP_LOGI(TAG,"transport init failed");
        ESP_ERROR_CHECK(ret);
    }


    
    peer_registry_config_t registry_config={.max_peers=2};

    peer_registry_interface_t* peer_registry=peer_registry_init(&registry_config);
    peer_registry->peer_registry_add_peer(GATE_NODE_ID,gate_node_mac,"gatenode");
    

    if(peer_registry==NULL)
        ESP_LOGI(TAG,"peer registry init failed");
    


    config_espnow_discovery discovery_config;
    //Must be an instance because config contains a pointer to it and 
    //unlike timer, peer_registry, its instance is not provided by any source
    
    esp_now_trasnsport_discovery_package_t* discovery_interface=esp_now_transport_get_discovery_interface();
    //This interface struct contaains complete package required by message service
    discovery_interface->peer_manager_interface.esp_now_transport_add_peer(gate_node_mac);
    
    database_interface_t database_interface = {.is_white_listed=peer_registry->peer_registry_exists_by_mac};

    discovery_config.database_interface=&database_interface;
    discovery_config.peer_manager_interface=&discovery_interface->peer_manager_interface;
    discovery_config.discovery_interface=&discovery_interface->discovery_interface;
    
    
    //Assign the discovery interface to the discovery member of discovery config
    discovery_config.discovery_duration=DISCOVERY_DURATION;
    discovery_config.discovery_interval=DISCOVERY_INTERVAL;
    
    ret=discovery_service_init(&discovery_config);

    ESP_LOGI(TAG,"discovery init init done");
    //Now since discovery interface is created and it returned the handlers. now thoose handlers will be assigned to callbacks

    /*These are the callbacks which the esp-now-comm components require to call on the event and now provided by this service component
    //These are set using the methods in the espnow_transport_interface because putting and then merely assigning
    as interface member wont work as the returned interface pointer is a copy of the original
    */

    
    //Message Service component
    //Assign the interface members required by the message service commponent
    message_codec_config_t message_codec_config;
    
    message_codec_config.database_interface=&database_interface;
    esp_now_trasnsport_msg_package_t* message_interface=esp_now_transport_get_msg_interface();
    message_codec_config.msg_interface=&message_interface->msg_interface;

    message_codec_init(&message_codec_config);


    user_interaction_config_t interaction_config={ .gate_close_endpoint="/close-gate",
                                                    .gate_open_endpoint="/open-gate",
                                                  
                                                };
    ret=user_interaction_create(&interaction_config);
    
    
    

    
    ESP_LOGI(TAG,"before register");
    //user_input->register_callback(home_node->user_command_callback_handler);
    ESP_LOGI(TAG,"after register");


    
    //void(*user_command)(user_command_t command);
    //user_command=home_node->user_command_callback_handler;

    start_discovery();
    
    //If OTA validation pending then validate now
    //if(ota_err==ERR_OTA_SERVICE_VALIDATION_PENDING)
      //  ota_set_valid(true);
    while(1){
        //user_command(USER_COMMAND_LOCK_CLOSE);
        vTaskDelay(pdMS_TO_TICKS(1000));
        //user_command(USER_COMMAND_LOCK_OPEN);
        //vTaskDelay(pdMS_TO_TICKS(500));
    }
    


}
