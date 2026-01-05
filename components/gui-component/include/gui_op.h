#ifndef GUI_OP_H
#define GUI_OP_H


#include "esp_err.h"
#include "gui_interface.h"

#ifdef __cplusplus
    extern "C" {
 #endif



//The name is misleading as if it is some gui event.
//Actually it is some system event for gui to display info about


esp_err_t gui_op_init();
gui_interface_t* gui_op_get_interface();





#ifdef __cplusplus
    }
    #endif

#endif