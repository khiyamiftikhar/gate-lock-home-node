#ifndef ROUTINE_EVENT_HANDLER_H
#define ROUTINE_EVENT_HANDLER_H


#include "esp_event.h"

void routine_event_handler (void *handler_arg,
                            esp_event_base_t base,
                            int32_t id,
                            void *event_data);

esp_err_t routine_handler_init();


#endif