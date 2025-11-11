#include "mdns_service.h"
#include "mdns.h"
#include "esp_err.h"
#include "esp_log.h"
//#include "sdkconfig.h"   // <-- Required to access Kconfig values

static const char *TAG = "mdns_service";

esp_err_t mdns_service_start(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return err;
    }

    const char *hostname = CONFIG_MDNS_HOSTNAME;
    const char *instance_name = CONFIG_MDNS_INSTANCE_NAME;

    mdns_hostname_set(hostname);
    mdns_instance_name_set(instance_name);
    mdns_service_add("ESP32-WebServer", "_http", "_tcp", 80, NULL, 0);

    ESP_LOGI(TAG, "mDNS started with hostname: %s.local, instance: %s", hostname, instance_name);
    return ESP_OK;
}
