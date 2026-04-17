#include "logger.h"
#include "log_capture.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd_mount.h"
#include "time_service.h"

#define LOG_FILE_PATH "/sdcard/loghome.txt"
#define CHUNK_SIZE    256

static TaskHandle_t s_task = NULL;
static uint32_t s_interval_ms = 4000;  // default 2 sec


static void sync_cb(const time_sync_result_t *res)
{
    if (!res->success) {
        ESP_LOGW("APP", "Time sync failed");
        return;
    }

    ESP_LOGI("APP",
        "Time synced! Jump: %ld sec (old=%lld new=%lld)",
        res->jump.delta_sec,
        (long long)res->jump.old_time,
        (long long)res->jump.new_time
    );
}

/* ---------- Internal Task ---------- */

static void sd_log_task(void *arg)
{
    log_snapshot_t snap={0};
    char buf[CHUNK_SIZE];

    FILE *f = NULL;

    while (1) {

        /* Try to open file if not open */
        if (!f) {
            f = fopen(LOG_FILE_PATH, "a");

            if (!f) {
                ESP_LOGW("SD_LOG", "Failed to open file, retrying...");
                vTaskDelay(pdMS_TO_TICKS(2000)); // retry delay
                continue;
            }

            ///ESP_LOGI("SD_LOG", "File opened successfully");
        }


        ///Adding Time stamp to logs
        char timestamp[TIME_SERVICE_STR_BUF_SIZE];

        if (time_service_now_str(5 * 3600, timestamp, sizeof(timestamp)) > 0) {
            fwrite(timestamp, 1, strlen(timestamp), f);
            fwrite("\n", 1, 1, f);   // next line after timestamp
        }
        ///Adding Time stamp to logs


        /* Take snapshot */
        log_snapshot_take(&snap);

        /* Write logs */
        size_t bytes_read;


        do{
            bytes_read=log_snapshot_read(&snap,buf,sizeof(buf)-1);
            //ESP_LOGI(TAG,"bytes read %d",bytes_read);
            if(bytes_read>0){
                buf[bytes_read]='\0';   //null terminate
                if (fwrite(buf, 1, bytes_read, f) != bytes_read) {
                    ESP_LOGE("SD_LOG", "Write failed!");

                    fflush(f);
                    fclose(f);
                    f = NULL;   // force reopen
                    break;
                }

            }
            else{
                fflush(f);
                fclose(f);
                f = NULL;   // force reopen
            }
        }while(bytes_read>0);


        vTaskDelay(pdMS_TO_TICKS(s_interval_ms));
    }
}
/* ---------- Public API ---------- */

bool sd_log_writer_start(uint32_t interval_ms)
{

    bool ret=sd_mount_init();
    time_init_result_t init_res;

    time_service_init(&init_res);

    if (!init_res.synced) {
        ESP_LOGW("APP", "Initial SNTP sync failed, using fallback time");
    }

    // --- Step 4: Trigger async sync (EXPLICIT)
    time_service_sync_async(sync_cb);


    if(ret==false){
        ESP_LOGE("SD_LOG","Failed to initialize SD card");
        return false;
        }


    if (s_task) {
        return false; // already running
    }

    s_interval_ms = interval_ms;

    BaseType_t res = xTaskCreate(
        sd_log_task,
        "sd_log",
        4096,
        NULL,
        5,
        &s_task
    );

    return (res == pdPASS);
}

void sd_log_writer_stop(void)
{
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
}