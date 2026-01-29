/*Dummy file used when non gui mode*/

#include <string.h>
#include "esp_log.h"
#include "gui_op.h"




static const char* TAG = "gpu op dummy";



static struct
{    
    bool init;
    gui_interface_t interface;
}gui_op={0};



gui_interface_t* gui_op_get_interface(){
    if(gui_op.init==true)
        return &gui_op.interface;
    else
        return NULL;
}




esp_err_t gui_inform(gui_event_t event, gui_event_data_t *evt_data)
{
 
    return ESP_OK;
}

esp_err_t gui_op_init(){


    
    gui_op.interface.gui_inform=gui_inform;
    gui_op.init=true;

    return ESP_OK;

}

