/* OTA example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "semaphore.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "errno.h"
#include "ota_service.h"


#define MAX_HTTP_RECV_BUFFER    1024
#define HASH_LEN                32 /* SHA-256 digest length */
#define OTA_RECV_TIMEOUT        5000
#define MANIFEST_URL            CONFIG_FIRMWARE_URL
#define AUTO_CHECK_DURATION     CONFIG_AUTO_CHECK_DURATION

static const char *TAG = "native_ota_example";
/*an ota data write buffer ready to write to the flash*/

typedef struct {
        char version[64];
        char firmware_url[512];
        char checksum[65];
        size_t firmware_size;
        bool update_available;
} manifest_t;

typedef esp_err_t (*big_data_handler_t)(const char *chunk, int len, void *ctx);
typedef struct{
    esp_partition_t *update_partition;
    char* buffer;
    int length;         //length of data written. used 2 times, in state struct and in this because necessary as big data handler only gets this context
    esp_ota_handle_t* update_handle;
    bool image_header_checked;
    bool ota_started;
    bool ota_finished;
    int bytes_written;
}ota_context_t;


static struct{
    //char ota_write_data[BUFFSIZE + 1];
    SemaphoreHandle_t start_update;
    bool validation_pending;        //It will stop the ota_task from progressing
    bool update_pending;            //The ota_task will set it true if it gets paused because of validation pending
    esp_http_client_handle_t client;
    char response_buffer[MAX_HTTP_RECV_BUFFER];
    int data_len;
    manifest_t manifest;
    bool saw_finish;
    bool expect_redirect;
    bool checked_status;
    big_data_handler_t big_data_handler;  //When content length is greater than buffer size (firmware file)
    ota_context_t ota_context;
    
    //TimerHandle_t timer;
    
}ota_service_state={0};



extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");



static void http_cleanup(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

static void __attribute__((noreturn)) task_fatal_error(void)
{
    ESP_LOGE(TAG, "Exiting task due to fatal error...");
    (void)vTaskDelete(NULL);

    while (1) {
        ;
    }
}

static void print_sha256 (const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s: %s", label, hash_print);
}

static void infinite_loop(void)
{
    int i = 0;
    ESP_LOGI(TAG, "When a new firmware is available on the server, press the reset button to download it");
    while(1) {
        ESP_LOGI(TAG, "Waiting for a new firmware ... %d", ++i);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}



static esp_err_t big_data_handler(const char *chunk, int len, void *ctx){

    
        //int len = esp_http_client_read(client, ota_write_data, MAX_HTTP_RECV_BUFFER);
        esp_err_t err=0;
        ota_context_t* ota_ctx=(ota_context_t*)ctx;
        char* ota_write_data=ota_ctx->buffer;
        esp_partition_t* update_partition=ota_ctx->update_partition;
        esp_ota_handle_t* update_handle = ota_ctx->update_handle;


        //ESP_LOGI(TAG,"data read length %d",len);
      
        if (len > 0) {
            if (ota_ctx->image_header_checked == false) {
                esp_app_desc_t new_app_info;
                if (len > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)){
                    
                    
                    //This code of checking header is redundant because version is checked using manifest
                 
                    memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                    ESP_LOGI(TAG, "New firmware version:");

                        //Commented because then running partition must be added to context
                    //esp_app_desc_t running_app_info;
                    //if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
                      //  ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
                   // }

                    ota_ctx->image_header_checked = true;

                    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, update_handle);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                        
                        
                        //Actually the policy is to continue but continue in this nested, so break and then continue
                        return ESP_FAIL;
                        //task_fatal_error();
                    }
                    ota_ctx->ota_started=true;
                    ESP_LOGI(TAG, "esp_ota_begin succeeded");
                } 
                //The header must be read as a whole not chunks (needs improvement in future). So consider it fail if len<header size
                else {
                    ESP_LOGE(TAG, "received package is not fit len");
                    return ESP_FAIL;
                }
            }
            
            //If the break statement didnt run it means data read > header size so write as ota
            //ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%"PRIx32,
              //  update_partition->subtype, update_partition->address);
            err = esp_ota_write( *update_handle, (const void *)chunk, len);
            if (err != ESP_OK) {
                esp_ota_abort(*update_handle);
                ota_ctx->ota_finished=false;
                return ESP_FAIL;
            }
            ota_ctx->length += len;
            ESP_LOGD(TAG, "Written image length %d", ota_ctx->length);
        } 
        
    return ESP_OK;
}








static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    
    //Since ota_service_state is a static struct so there is not type for it
    //The state can be accessed globally, but instead using it this way will make this handler resuable
    typeof(ota_service_state)* ctx= (typeof(ota_service_state)*) evt->user_data;

    //ESP_LOGI(TAG, "ctx ptr=%p expect_redirect=%d", ctx, ctx ? ctx->expect_redirect : -1);
    switch (evt->event_id) {

    case HTTP_EVENT_ON_HEADER:
        if (strcasecmp(evt->header_key, "Location") == 0) {
            // Copy redirect URL (full header value is available)
            ESP_LOGI(TAG,"location");
            //ESP_LOGI(TAG, "Header: %s = %s", evt->header_key, evt->header_value);
            strncpy(ctx->response_buffer, evt->header_value, MAX_HTTP_RECV_BUFFER - 1);
            ctx->response_buffer[MAX_HTTP_RECV_BUFFER - 1] = '\0';
            ctx->data_len = strlen(ctx->response_buffer);
            ctx->expect_redirect=true;
            ESP_LOGI(TAG,"leaving location");
        }
        break;

    case HTTP_EVENT_ON_DATA: {
        // First time we see data → check status
        
        
        int status = esp_http_client_get_status_code(evt->client);

        if(status >= 300 && status < 400)
            break;
        

        else if (ctx->expect_redirect) {
            // ignore body on redirect
            break;
        }

        //ESP_LOGI(TAG,"proceeding for data");

        int content_length = esp_http_client_get_content_length(evt->client);

        if ((content_length > MAX_HTTP_RECV_BUFFER || content_length == -1) && ctx->big_data_handler) {
            // Stream chunks directly
            //This is yet another user ctx for the big data. useful here for maintaning index
            esp_err_t res = ctx->big_data_handler(evt->data, evt->data_len,(void*)&ctx->ota_context);
            if (res != ESP_OK) {
                ESP_LOGE("HTTP", "Big data handler failed → aborting");
                return res;
            }
        } else {
            // Accumulate into local buffer
            if (ctx->data_len + evt->data_len < MAX_HTTP_RECV_BUFFER) {
                memcpy(ctx->response_buffer + ctx->data_len, evt->data, evt->data_len);
                ctx->data_len += evt->data_len;
                ctx->response_buffer[ctx->data_len] = '\0';
            } else {
                ESP_LOGE("HTTP", "Buffer overflow risk, aborting");
                return ESP_FAIL;
            }
        }
        break;
    }
        
    case HTTP_EVENT_ON_FINISH:
        if(ctx->ota_context.ota_started==true){
            //OTA finished successfully
            ctx->ota_context.ota_finished=true;
        }
        ctx->saw_finish = true;
        break;

    case HTTP_EVENT_DISCONNECTED:

        //If disconnet comes before finish then its a  problem
        if (!ctx->saw_finish && ctx->ota_context.ota_started==true) {
            esp_ota_abort(*ctx->ota_context.update_handle);
            return ESP_FAIL;
        }

        else if (!ctx->saw_finish) {
            ESP_LOGE("HTTP", "Disconnected before finish → error");
            return ESP_FAIL;
        }
        break;

    default:
        break;
    }

    return ESP_OK;
}

// assume this is allocated globally or in your OTA state struct

static esp_err_t fetch_ota_manifest(const char *manifest_url,manifest_t* manifest) {
    if (!manifest_url || !manifest) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_handle_t client=ota_service_state.client;

    memset(&ota_service_state.manifest, 0, sizeof(ota_service_state.manifest));
    memset(ota_service_state.response_buffer, 0, sizeof(ota_service_state.response_buffer));
    ota_service_state.data_len=0;

    // set new url for this request
    esp_http_client_set_url(client, manifest_url);
    ESP_LOGI(TAG,"url %s",manifest_url);
    //Although events are handled through event handler but it iis a blocking call
    esp_err_t err = esp_http_client_perform(client);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
        return err;
    }

    //int content_len = esp_http_client_fetch_headers(client);
    //ESP_LOGI(TAG, "Manifest content length = %d", content_len);

    //int total_read = 0;
    
    /*
    while (1) {
        int bytes_read = esp_http_client_read(
            client,
            ota_service_state.response_buffer + total_read,
            MAX_HTTP_RECV_BUFFER - total_read - 1);

        if (bytes_read < 0) {
            ESP_LOGE(TAG, "Read error");
            err = ESP_FAIL;
            break;
        } else if (bytes_read == 0) {
            if (esp_http_client_is_complete_data_received(client)) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        } else {
            total_read += bytes_read;
            if (total_read >= MAX_HTTP_RECV_BUFFER - 1) {
                ESP_LOGW(TAG, "Manifest truncated to %d bytes", total_read);
                break;
            }
        }
    }
        */

        // After reading 1023 bytes, log the actual content
    ESP_LOGI(TAG, "First 200 chars:");
    ESP_LOG_BUFFER_CHAR(TAG, ota_service_state.response_buffer, 200);

    ESP_LOGI(TAG, "Last 200 chars of truncated data:");
    ESP_LOG_BUFFER_CHAR(TAG, ota_service_state.response_buffer + 823, 200);

    
    ota_service_state.response_buffer[ota_service_state.data_len] = '\0';
    ESP_LOGI(TAG, "Manifest received (%d bytes)", ota_service_state.data_len);

    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "Bad HTTP status = %d", status);
        err = ESP_FAIL;
    } else {
        cJSON *json = cJSON_Parse(ota_service_state.response_buffer);
        if (!json) {
            ESP_LOGE(TAG, "JSON parse failed");
            err = ESP_FAIL;
        } else {
            cJSON *jver = cJSON_GetObjectItem(json, "version");
            if (cJSON_IsString(jver)) {
                strncpy(manifest->version, jver->valuestring, sizeof(manifest->version)-1);
            }
            cJSON *jurl = cJSON_GetObjectItem(json, "firmware_url");
            if (cJSON_IsString(jurl)) {
                strncpy(manifest->firmware_url, jurl->valuestring, sizeof(manifest->firmware_url)-1);
            }
            cJSON *jck = cJSON_GetObjectItem(json, "checksum");
            if (cJSON_IsString(jck)) {
                strncpy(manifest->checksum, jck->valuestring, sizeof(manifest->checksum)-1);
            }
            cJSON *jsz = cJSON_GetObjectItem(json, "firmware_size");
            if (cJSON_IsNumber(jsz)) {
                manifest->firmware_size = jsz->valueint;
            }
            cJSON_Delete(json);
        }
    }

    esp_http_client_close(client); // keep client for reuse

    return err;
}


static esp_err_t is_update_available(char* new_version){

    
    if (new_version==NULL || new_version[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret=0;
    const esp_partition_t *running = esp_ota_get_running_partition();

    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running version: %s", running_app_info.version);

    } else {
        //Since unable to read so assume , new firmware is updated, otherwise forever stuck in current version
        ESP_LOGE(TAG, "Failed to read running partition description");
        
        ret=ESP_OK;
        return ret;
    }

    

    
    
    int comparison = strcmp(new_version, running_app_info.version);

    if (comparison > 0) {
        ret = true;
    }

    return ret;
}


static esp_err_t detect_last_invalid_app(const char *new_version){

    const esp_partition_t *last_invalid_app = esp_ota_get_last_invalid_partition();
    if (last_invalid_app == NULL) {
        ESP_LOGI(TAG, "No invalid app partition found");
        return ESP_OK;
    }

    esp_app_desc_t invalid_app_info;
    esp_err_t err = esp_ota_get_partition_description(last_invalid_app, &invalid_app_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not read description of last invalid app partition (err=%s)",
                 esp_err_to_name(err));
        return ESP_FAIL;   // generic read failure
    }

    ESP_LOGI(TAG, "Last invalid firmware version: %s", invalid_app_info.version);

    if (strcmp(invalid_app_info.version, new_version) == 0) {
        ESP_LOGW(TAG, "New version is the same as invalid version (%s). Skipping OTA.",
                 invalid_app_info.version);
        return ERR_OTA_INVALID_VERSION;  // special case
    }

    return ESP_OK;
}


static void ota_task(void *pvParameter)
{
    esp_err_t err=0;
    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;
    esp_http_client_handle_t client=ota_service_state.client;
    manifest_t* manifest=&ota_service_state.manifest;
    char* ota_write_data=ota_service_state.response_buffer;
    char* manifest_url=MANIFEST_URL;
    ESP_LOGI(TAG, "Starting OTA example task");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();


    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08"PRIx32", but running from offset 0x%08"PRIx32,
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08"PRIx32")",
             running->type, running->subtype, running->address);

    



    while(1){
    
        //Wait till either signal arrives or time expires
        BaseType_t sem_result = xSemaphoreTake( ota_service_state.start_update,pdMS_TO_TICKS(24UL * 60UL * 60UL * 1000UL) );// 24h 

        if (sem_result != pdTRUE) {
            ESP_LOGI(TAG, "24 hours passed, running update.");
        } else {
            ESP_LOGI(TAG, "Semaphore signaled, running update.");
        }

        //If validation pending then go back
        if (ota_service_state.validation_pending==true)
        {
            ota_service_state.update_pending=true;
            continue;
        }
        
        ota_service_state.update_pending=false;

        esp_err_t err = fetch_ota_manifest(manifest_url, manifest);

        //Skip if manifest not available and wait again
        if(err!=ESP_OK)
            continue;

        //if last used invalid ap detected then skip
        if(detect_last_invalid_app(manifest->version)==ERR_OTA_INVALID_VERSION){
            //Skip the update
            continue;
        }

        //If the available version is not newer, then also continue
        if(is_update_available(manifest->version)!=ESP_OK)
            continue;

        ESP_LOGI(TAG,"url %s",manifest->firmware_url);
        

        esp_http_client_set_url(client,manifest->firmware_url);
        //esp_http_client_set_redirection(client);
        //clear the buffer
        memset(ota_service_state.response_buffer, 0, sizeof(ota_service_state.response_buffer));
        //Setting these to inital value before next request
        ota_service_state.expect_redirect=false;
        ota_service_state.data_len=0;
        
        err = esp_http_client_perform(client);
        //If unable to open connection then too skip an go back to waiting
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed firmware link: %s", esp_err_to_name(err));
            continue;
            //task_fatal_error();
        }
        
        //The github firmware link redirects to  another link

            // Now open the redirected URL
        
        //The response buffer contains the redirect url
        //Now downloading firmware and big data handler will  be used
        
        update_partition = esp_ota_get_next_update_partition(NULL);
        //assert(update_partition != NULL);
        //Gp back to waiting if update partition is NULL
        if(update_partition == NULL){
            continue;
        }
        ESP_LOGI(TAG,"url %.10s",ota_service_state.response_buffer);
        esp_http_client_set_url(client,ota_service_state.response_buffer);
        memset(ota_service_state.response_buffer, 0, sizeof(ota_service_state.response_buffer));
        //Setting these to inital value before next request
        ota_service_state.expect_redirect=false;
        ota_service_state.data_len=0;
        ota_service_state.ota_context.buffer=ota_service_state.response_buffer;
        ota_service_state.ota_context.update_handle=&update_handle;
        ota_service_state.ota_context.update_partition=update_partition;
        err = esp_http_client_perform(client);
        
        
        
        ESP_LOGI(TAG, "Total Write binary data length: %d", ota_service_state.ota_context.length);
        if (ota_service_state.ota_context.ota_finished != true) {
             continue;
         }

        err = esp_ota_end(update_handle);
        if (err != ESP_OK) {
            if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Image validation failed, image is corrupted");
            } else {
                ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
            }
         
            continue;
        }

        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
            continue;
            //http_cleanup(client);
            //task_fatal_error();
        }
        ESP_LOGI(TAG, "Prepare to restart system!");
        esp_restart();
        return ;
    }
}



bool update_pending(){
    return ota_service_state.update_pending;

}

static esp_err_t send_update_process_start_signal(){

    xSemaphoreGive(ota_service_state.start_update);
    return ESP_OK;
}



esp_err_t ota_set_valid(bool valid){
    if(valid){
        esp_ota_mark_app_valid_cancel_rollback();
        //Update maybe pending if previous was stopped because of an earlier verification pending
        if(update_pending())
            send_update_process_start_signal();
    }
    else
        esp_ota_mark_app_invalid_rollback_and_reboot();
    
    return ESP_OK;
}


esp_err_t ota_service_init(){
    ESP_LOGI(TAG, "OTA example app_main start");

esp_http_client_config_t config={0};

    config.url = "http://example.com";    //Some random address which will be replaced before request
    config.cert_pem = (char *)server_cert_pem_start;
    config.timeout_ms = OTA_RECV_TIMEOUT;
    config.keep_alive_enable = true;
    config.buffer_size = 8192;    // Explicity supplied large value instead of not setting it and thus using default size, because github is sending a big header in responce which contains redirect url
    config.buffer_size_tx = 512;  // request side can stay small
    config.disable_auto_redirect=true;
    config.event_handler=_http_event_handler;
    config.user_data=&ota_service_state;
    config.buffer_size = 2048;           // body buffer
    config.buffer_size_tx = 2048;        // optional, for sending
    
    

    
    esp_http_client_handle_t* client = &ota_service_state.client;
    *client = esp_http_client_init(&config);
    if (*client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        return ERR_OTA_SERVICE_INIT_FAIL;
    }
 
    ota_service_state.big_data_handler=big_data_handler;
     ota_service_state.start_update=xSemaphoreCreateBinary();

    if(ota_service_state.start_update==NULL)
        return ERR_OTA_SERVICE_INIT_FAIL;

 
    //Task creation at end so that the client handle and semaphore are created before it
    BaseType_t ret;
    ret=xTaskCreate(&ota_task, "ota_task", 8192, NULL, 5, NULL);
    if(ret==pdFAIL)
        return ERR_OTA_SERVICE_INIT_FAIL;

   



    
    /*
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        task_fatal_error();
    }
    
    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

    // get sha256 digest for the partition table
    partition.address   = ESP_PARTITION_TABLE_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_MAX_LEN;
    partition.type      = ESP_PARTITION_TYPE_DATA;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for the partition table: ");

    // get sha256 digest for bootloader
    partition.address   = ESP_BOOTLOADER_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_OFFSET;
    partition.type      = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");
    */
   
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGE(TAG, "Firmware verification pending ...");
            ota_service_state.validation_pending=true;
            return ERR_OTA_SERVICE_VALIDATION_PENDING;
        }
    }

    send_update_process_start_signal();
    return ESP_OK;
    
}