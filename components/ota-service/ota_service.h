#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H


#include "esp_err.h"
#include "event_system_adapter.h"


#define     ERR_OTA_SERVICE_BASE                        0
#define     ERR_OTA_SERVICE_INIT_FAIL                   (ERR_OTA_SERVICE_BASE-1)
#define     ERR_OTA_SERVICE_VALIDATION_PENDING          (ERR_OTA_SERVICE_BASE+5)
#define     ERR_OTA_INVALID_VERSION                     (ERR_OTA_SERVICE_BASE-2)



DECLARE_EVENT_ADAPTER(OTA_SERVICE);


#define     OTA_SERVICE_ROUTINE_EVENT_REBOOT_REQUIRED           1

#define     OTA_SERVICE_ROUTINE_EVENT_VERIFICATION_PENDING      2



esp_err_t ota_set_valid(bool valid);
esp_err_t ota_service_init();











#endif