#include "logger.h"
#include "log_capture.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LOG_FILE_PATH "/sdcard/log.txt"
#define CHUNK_SIZE    256

static TaskHandle_t s_task = NULL;
static uint32_t s_interval_ms = 2000;  // default 2 sec

/* ---------- Internal Task ---------- */

static void sd_log_task(void *arg)
{
    FILE *f = fopen(LOG_FILE_PATH, "a");
    if (!f) {
        printf("SD_LOG: Failed to open file\n");
        vTaskDelete(NULL);
        return;
    }

    log_snapshot_t snap;
    char buf[CHUNK_SIZE];

    while (1) {
        /* Take snapshot */
        log_snapshot_take(&snap);

        /* Read all available data */
        size_t n;
        while ((n = log_snapshot_read(&snap, buf, sizeof(buf))) > 0) {
            fwrite(buf, 1, n, f);
        }

        /* Flush periodically */
        fflush(f);

        ESP_LOGI("SD_LOG", "Flushed to SD card");
        vTaskDelay(pdMS_TO_TICKS(s_interval_ms));
    }
}

/* ---------- Public API ---------- */

bool sd_log_writer_start(uint32_t interval_ms)
{
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