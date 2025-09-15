
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
#include "discovery_timer.h"
#include "peer_registry.h"
#include "home_node.h"
#include "user_buttons.h"
#include "user_output.h"
#include "smartconfig.h"
#include "server_adapter.h"


#define     ESPNOW_CHANNEL          1
#define     DISCOVERY_DURATION      15000    //ms
#define     DISCOVERY_INTERVAL      2000    //ms
#define     ESPNOW_ENABLE_LONG_RANGE    1
static const char* TAG="main gate";

const uint8_t gate_node_mac[]={0xe4,0x65,0xb8,0x1b,0x1c,0xd8};

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
}

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
    esp_flash_init();
    //wifi_init();
    wifi_smartconfig_t wifi_cfg={.callback=init_espnow};

    initialise_wifi(&wifi_cfg);
    
    //Wait until wifi connection is established
    while(proceed==false){
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    uint8_t primary;
    wifi_second_chan_t second;
    ESP_ERROR_CHECK(esp_wifi_get_channel(&primary, &second));
    
    esp_now_transport_config_t transport_config={.wifi_channel=primary};

    //The objcts created but callbacks not assigned. will be assigned later
    esp_now_trasnsport_interface_t* espnow_transport=esp_now_transport_init(&transport_config);

    if(espnow_transport==NULL)
        ESP_LOGI(TAG,"transport init failed");
    
    peer_registry_config_t registry_config={.max_peers=2};

    peer_registry_interface_t* peer_registry=peer_registry_init(&registry_config);
    peer_registry->peer_registry_add_peer(GATE_NODE_ID,gate_node_mac,"gatenode");
    espnow_transport->esp_now_transport_add_peer(gate_node_mac);

    if(peer_registry==NULL)
        ESP_LOGI(TAG,"peer registry init failed");
    

    discovery_timer_implementation_t* timer_interface=timer_create(DISCOVERY_INTERVAL);

    if(timer_interface==NULL)
        ESP_LOGI(TAG,"timer init has failed");

    config_espnow_discovery discovery_config;
    //Must be an instance because config contains a pointer to it and 
    //unlike timer, peer_registry, its instance is not provided by any source
    discovery_comm_interface_t discovery_comm_interface;
    
    //Assign methods required by the discovery service component provided by the esp-now-comm component
    discovery_comm_interface.acknowledge_the_discovery=espnow_transport->esp_now_transport_send_discovery_ack;
    discovery_comm_interface.add_peer=espnow_transport->esp_now_transport_add_peer;
    discovery_comm_interface.send_discovery=espnow_transport->esp_now_transport_send_discovery;
    discovery_comm_interface.process_discovery_completion_callback=NULL;

    //Assign the discovery interface to the discovery member of discovery config
    discovery_config.discovery=&discovery_comm_interface;
    discovery_config.timer=&timer_interface->methods;
    //The white list interface member assigned to the peer registry appropriate method
    discovery_whitelist_interface_t white_list;
    white_list.is_white_listed=peer_registry->peer_registry_exists_by_mac;
    discovery_config.whitelist=&white_list;
    discovery_config.discovery_duration=DISCOVERY_DURATION;
    discovery_config.discovery_interval=DISCOVERY_INTERVAL;
    
    discovery_service_interface_t* discovery_handlers=discovery_service_init(&discovery_config);

    //Now since discovery interface is created and it returned the handlers. now thoose handlers will be assigned to callbacks

    /*These are the callbacks which the esp-now-comm components require to call on the event and now provided by this service component
    //These are set using the methods in the espnow_transport_interface because putting and then merely assigning
    as interface member wont work as the returned interface pointer is a copy of the original
    */

    espnow_transport->callbacks.on_device_discovered=discovery_handlers->comm_callback_handler.process_discovery_callback;
    espnow_transport->callbacks.on_discovery_ack=discovery_handlers->comm_callback_handler.process_discovery_acknowledgement_callback;
    
    timer_interface->callback_handler=discovery_handlers->timer_callback_handler.timer_handler;
    


    //Message Service component
    //Assign the interface members required by the message service commponent
    home_node_config_t home_config;
    
    //This is redndant and needs to be optimized. discovery component has the same innterface
    node_white_list_interface_t node_white_list;
    node_white_list.is_in_whitelist=peer_registry->peer_registry_exists_by_mac;
    //Must be an instance because config contains a pointer to it and 
    //unlike timer, peer_registry, its instance is not provided by any source
    home_config.white_list=&node_white_list;
    node_msg_interface_t msg_interface;
    msg_interface.send_msg=espnow_transport->esp_now_transport_send_data;
    //The remaining callbacks of the esp_now_comm to the deserving targets
    //So now esp_now_comm will invoke methods inside the message service sources
    gate_node_id_interface_t gate_node_id;
    gate_node_id.get_gate_node_mac=get_gate_node_mac;
    
    /*
    user_input_config_t user_config={.open_button_gpio_no=5,
                                .close_button_gpio_no=21,
                                   };
    */
    
    //user_input_interface_t* user_input=user_input_create(&user_config);
    //user_interaction.inform_command_status=inform_command_status;
    //user_interaction.inform_lock_status=inform_lock_status;
    
    //user_output_interface_t* user_output=user_output_create();


    user_interaction_config_t interaction_config={ .gate_close_endpoint="/close-gate",
                                                    .gate_open_endpoint="/open-gate",
                                                    //.handler=home_node->user_command_callback_handler
                                                };
    user_interaction_interface_t* user_interface=user_interaction_create(&interaction_config);
    home_config.gate_node_id=&gate_node_id;
    home_config.msg_interface=&msg_interface;
    home_config.user_output=&user_interface->user_output;

    home_node_service_interface* home_node= home_node_service_create(&home_config);
    
    user_interface->register_user_command_callback(home_node->user_command_callback_handler);
    
    

    espnow_transport->callbacks.on_data_received=home_node->msg_received_handler;
    espnow_transport->callbacks.on_send_done=home_node->msg_sent_handler;
    ESP_LOGI(TAG,"before register");
    //user_input->register_callback(home_node->user_command_callback_handler);
    ESP_LOGI(TAG,"after register");


    
    //void(*user_command)(user_command_t command);
    //user_command=home_node->user_command_callback_handler;

    start_discovery();

    while(1){
        //user_command(USER_COMMAND_LOCK_CLOSE);
        vTaskDelay(pdMS_TO_TICKS(5000));
        //user_command(USER_COMMAND_LOCK_OPEN);
        //vTaskDelay(pdMS_TO_TICKS(500));
    }
    


}
