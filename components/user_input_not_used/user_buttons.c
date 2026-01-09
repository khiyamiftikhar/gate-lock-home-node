#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "user_buttons.h"
#include "iot_button.h"
#include "button_gpio.h"


static const char* TAG="user input";
#define BUTTON_ACTIVE_LEVEL   0

static struct{

    uint8_t open_button_gpio_no;
    uint8_t close_button_gpio_no;
    user_input_interface_t interface;
    user_command_callback cb;


}user_input_state={0};



static void button_event_cb(void *arg, void *data)
{
    button_event_t event = iot_button_get_event(arg);
    uint8_t button_gpio=(uint8_t) data;
    

    if(button_gpio==user_input_state.open_button_gpio_no){
        ESP_LOGI(TAG,"open button pressed");    
        user_input_state.cb(USER_COMMAND_LOCK_OPEN);    
        
    }
    else if(button_gpio==user_input_state.close_button_gpio_no){
        ESP_LOGI(TAG,"close button pressed");
        user_input_state.cb(USER_COMMAND_LOCK_CLOSE);    
        
    }

    /*
    ESP_LOGI(TAG, "%s", iot_button_get_event_str(event));
    if (BUTTON_PRESS_REPEAT == event || BUTTON_PRESS_REPEAT_DONE == event) {
        ESP_LOGI(TAG, "\tREPEAT[%d]", iot_button_get_repeat(arg));
    }

    if (BUTTON_PRESS_UP == event || BUTTON_LONG_PRESS_HOLD == event || BUTTON_LONG_PRESS_UP == event) {
        ESP_LOGI(TAG, "\tTICKS[%"PRIu32"]", iot_button_get_ticks_time(arg));
    }

    if (BUTTON_MULTIPLE_CLICK == event) {
        ESP_LOGI(TAG, "\tMULTIPLE[%d]", (int)data);
    }*/
}

/*

static esp_err_t inform_lock_status(lock_status_t status){
    ESP_LOGI(TAG,"Lock status not implemented");
    return 0;
}
    //When command is succesfully sent
static esp_err_t inform_command_status(bool success){
    ESP_LOGI(TAG,"command status not implemented");
    return 0;

}*/

static esp_err_t register_callback(user_command_callback cb){

    if(cb==NULL)
        return ESP_FAIL;
    user_input_state.cb=cb;
    return ESP_OK;

}








static esp_err_t button_create(uint8_t gpio_num){
    const button_config_t btn_cfg = {0};
    //user_input_state.open_button_gpio_no=config->open_button_gpio_no;
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = gpio_num,
        .active_level = BUTTON_ACTIVE_LEVEL,
    };


    
    button_handle_t btn = NULL;
    
    esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &btn);
    ESP_ERROR_CHECK(ret!= ESP_OK);
    ESP_ERROR_CHECK(btn==NULL);
    iot_button_register_cb(btn, BUTTON_PRESS_DOWN, NULL, button_event_cb, (void*)gpio_num);
    
    /*iot_button_register_cb(btn, BUTTON_PRESS_UP, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_PRESS_REPEAT, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_PRESS_REPEAT_DONE, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_DOUBLE_CLICK, NULL, button_event_cb, NULL);
    

    iot_button_register_cb(btn, BUTTON_MULTIPLE_CLICK, &args, button_event_cb, (void *)2);
    */
    /*!< Triple Click */
    /*
    args.multiple_clicks.clicks = 3;
    iot_button_register_cb(btn, BUTTON_MULTIPLE_CLICK, &args, button_event_cb, (void *)3);
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_HOLD, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_UP, NULL, button_event_cb, NULL);
    iot_button_register_cb(btn, BUTTON_PRESS_END, NULL, button_event_cb, NULL);
    */
    
    //iot_button_delete(btn);
    return 0;
}




user_input_interface_t* user_input_create(user_input_config_t* config){

    if(config==NULL)
        return NULL;

    user_input_state.close_button_gpio_no=config->close_button_gpio_no;
    user_input_state.open_button_gpio_no=config->open_button_gpio_no;
    user_input_state.interface.register_callback=register_callback;
    //user_input_state.cb=config->callback;
    //user_input_state.interface.inform_command_status=inform_command_status;
   // user_input_state.interface.inform_command_status=inform_lock_status;
    button_create(user_input_state.close_button_gpio_no);
    button_create(user_input_state.open_button_gpio_no);


    return &user_input_state.interface;
}

