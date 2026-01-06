/* OTA example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <sys/time.h>

#include <time.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
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

DEFINE_EVENT_ADAPTER(OTA_SERVICE);

typedef struct {
        char version[64];
        char firmware_url[512];
        char checksum[65];
        size_t firmware_size;
        bool update_available;
} manifest_t;


static struct{
    //char ota_write_data[BUFFSIZE + 1];
    SemaphoreHandle_t start_update;
    bool validation_pending;        //It will stop the ota_task from progressing
    bool update_pending;            //The ota_task will set it true if it gets paused because of validation pending
    esp_http_client_handle_t client;
    char response_buffer[MAX_HTTP_RECV_BUFFER];
    manifest_t manifest;
    int data_len;
    bool expect_redirect;
    TaskHandle_t ota_task_handle;
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


///

/// @brief Only purpose of this event handler is to read the redirect header. 
/// All other events are handles synchronously in the ota_task
/// @param evt 
/// @return 
static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    
    //Since ota_service_state is a static struct so there is not type for it
    //The state can be accessed globally, but instead using it this way will make this handler resuable
    typeof(ota_service_state)* ctx= (typeof(ota_service_state)*) evt->user_data;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
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
            //Notify the waiting taask
            
            //BaseType_t notify_result;
            
            
            //notify_result = xTaskNotifyFromISR(ctx->ota_task_handle, 0,eSetValueWithOverwrite,&xHigherPriorityTaskWoken);
    
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

    // set new url for this request
    esp_http_client_set_url(client, manifest_url);
    ESP_LOGI(TAG,"url %s",manifest_url);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
        return err;
    }

    int content_len = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG, "Manifest content length = %d", content_len);

    int total_read = 0;
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

        // After reading 1023 bytes, log the actual content
    ESP_LOGI(TAG, "First 200 chars:");
    ESP_LOG_BUFFER_CHAR(TAG, ota_service_state.response_buffer, 200);

    ESP_LOGI(TAG, "Last 200 chars of truncated data:");
    ESP_LOG_BUFFER_CHAR(TAG, ota_service_state.response_buffer + 823, 200);

    
    ota_service_state.response_buffer[total_read] = '\0';
    ESP_LOGI(TAG, "Manifest received (%d bytes)", total_read);

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


static bool is_update_available(char* new_version){

    
    if (new_version==NULL || new_version[0] == '\0') {
        return false;
    }
    esp_err_t ret=0;
    const esp_partition_t *running = esp_ota_get_running_partition();

    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running version: %s", running_app_info.version);

    } else {
        //Since unable to read so assume , new firmware is updated, otherwise forever stuck in current version
        ESP_LOGE(TAG, "Failed to read running partition description");
        
        
        return false;
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

    ota_service_state.ota_task_handle=xTaskGetCurrentTaskHandle();

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
        if(!is_update_available(manifest->version))
            continue;

        ESP_LOGI(TAG,"url %s",manifest->firmware_url);
        
        esp_http_client_set_url(client,manifest->firmware_url);
        //This one is perform, beacuse open is not working in blocking way with event handler
        err = esp_http_client_perform(client);
        //If unable to open connection then too skip an go back to waiting
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            esp_http_client_close(client);
            continue;
            //task_fatal_error();
        }
     
        //Wait For notification from the event handler before proceeding. so that redirect url is captured
        ESP_LOGI(TAG,"waiting for notification");
        //xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
        ESP_LOGI(TAG,"wait over");
        esp_http_client_close(client);
        /*
        //Added below because of git redirection
        //When get status was called after fetch header, the get header later was not giving anythin
        //Seems internally buffer was replaced by some new value. so now fetch header is called later
        int content_length=esp_http_client_fetch_headers(client);
        int status_code;
        status_code= esp_http_client_get_status_code(client);
        
    
        
        ESP_LOGI(TAG, "firmware HTTP Status: %d, Content-Length: %d", status_code, content_length);
        */
        //Handle redirect responses - NOW call esp_http_client_set_redirection()
        
        

        if (ota_service_state.expect_redirect==true){
            
            
            ESP_LOGI(TAG, "Got redirect response, following redirect...");
            //content_length=esp_http_client_fetch_headers(client);

            
            
            
            // CORRECT USAGE: Call set_redirection AFTER receiving 30x response
            
            
            //char *location = NULL;      //redirect url
            //err = esp_http_client_get_header(client, "Location", &location);

            /*
            if (err != ESP_OK) {
                ESP_LOGI(TAG, "Failed to set redirection: %s", esp_err_to_name(err));
                continue;
            }
            else if(location==NULL){
                ESP_LOGI(TAG, "Failed to get redirection URL");
                continue;
            }

            
            size_t loc_len = strnlen(location, 1024);  // sanity limit, in case no '\0'
            size_t copy_len = MIN(loc_len, sizeof(ota_service_state.response_buffer) - 1);
            memcpy(ota_service_state.response_buffer, location, copy_len);
            ota_service_state.response_buffer[copy_len] = '\0';  //ensure termination
            //ESP_LOGI(TAG, "Redirect URL: %s", ota_service_state.response_buffer);
            */
            esp_http_client_close(client);//close from current

            //The response buffer contains the redirect url
            esp_http_client_set_url(client,ota_service_state.response_buffer); 

            ESP_LOGI(TAG,"url final %s",ota_service_state.response_buffer);
            


            // Now open the redirected URL
            err = esp_http_client_open(client, 0);
            //Clear it because ota firmware will be copied in it
            memset(ota_service_state.response_buffer, 0, sizeof(ota_service_state.response_buffer));

            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to open redirected connection: %s", esp_err_to_name(err));
                esp_http_client_close(client);
                continue;
            }
            
            int status_code = esp_http_client_get_status_code(client);
            int content_length = esp_http_client_fetch_headers(client);
            ESP_LOGI(TAG, "why not coming");
            ESP_LOGI(TAG, "After redirect - Status: %d, Content-Length: %d", status_code, content_length);
        }
        
        
        update_partition = esp_ota_get_next_update_partition(NULL);
        //assert(update_partition != NULL);
        //Gp back to waiting if update partition is NULL
        if(update_partition == NULL){
            continue;
        }


        ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%"PRIx32,
                update_partition->subtype, update_partition->address);

        int binary_file_length = 0;
        /*deal with all receive packet*/
        bool image_header_was_checked = false;
        bool read_success=false;
        uint8_t attempts=0;     // a workaround to read the chunked data from the firmware url. forst 0 read length will be ignored
        while (1) {
            int data_read = esp_http_client_read(client, ota_write_data, MAX_HTTP_RECV_BUFFER);
            //ESP_LOGI(TAG,"data read length %d",data_read);
            if (data_read < 0) {
                ESP_LOGE(TAG, "Error: SSL data read error");
                read_success=false;
                esp_http_client_close(client);
                //Actually the policy is to continue but continue in this nested, so break and then continue
                break;
                //task_fatal_error();
            } else if (data_read > 0) {
                if (image_header_was_checked == false) {
                    esp_app_desc_t new_app_info;
                    if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)){
                        // check current version with downloading
                        memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                        ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                        esp_app_desc_t running_app_info;
                        if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
                            ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
                        }

                        image_header_was_checked = true;

                        err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                            read_success=false;
                            esp_http_client_close(client);
                            //Actually the policy is to continue but continue in this nested, so break and then continue
                            break;
                            //task_fatal_error();
                        }
                        ESP_LOGI(TAG, "esp_ota_begin succeeded");
                    } 
					//The header must be read as a whole not chunks (needs improvement in future). So consider it fail if data_read<header size
					else {
                        ESP_LOGE(TAG, "received package is not fit len");
                        read_success=false;
                        esp_http_client_close(client);
						break;
                    }
                }
				
				//If the break statement didnt run it means data read > header size so write as ota
                err = esp_ota_write( update_handle, (const void *)ota_write_data, data_read);
                if (err != ESP_OK) {
                    read_success=false;
                    esp_http_client_close(client);
                    esp_ota_abort(update_handle);
					break;
                }
                binary_file_length += data_read;
                ESP_LOGD(TAG, "Written image length %d", binary_file_length);
            } else if (data_read == 0) {
            /*
                * As esp_http_client_read never returns negative error code, we rely on
                * `errno` to check for underlying transport connectivity closure if any
                */
                if(attempts==0){     //A workaround to ignore the first 0 read incase of chunked data which is the case for firmware
                    attempts++;
                    continue;
                }

                if (errno == ECONNRESET || errno == ENOTCONN) {
                    read_success=false;
                    ESP_LOGE(TAG, "Connection closed, errno = %d", errno);
                    break;
                }
                if (esp_http_client_is_complete_data_received(client) == true) {
                    read_success=true;
                    ESP_LOGI(TAG, "Connection closed bcz completed");
                    break;
                }
            }

            
            
        }

        //If there was read_success=failure in the while loop and thus break statement which meant skip so continue
        if(read_success==false){
            continue;
        }
        
            ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);
        if (esp_http_client_is_complete_data_received(client) != true) {
            ESP_LOGE(TAG, "Error in receiving complete file");
        
            esp_http_client_close(client);
            esp_ota_abort(update_handle);
            continue;
            //break;
            //task_fatal_error();
        }

        err = esp_ota_end(update_handle);
        if (err != ESP_OK) {
            if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Image validation failed, image is corrupted");
            } else {
                ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
            }
            esp_http_client_close(client);
            //http_cleanup(client);
            //task_fatal_error();
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
        OTA_SERVICE_post_event(OTA_SERVICE_ROUTINE_EVENT_REBOOT_REQUIRED,NULL,0);
        //esp_restart();
        //return ;
    }
}



bool update_pending(){
    return ota_service_state.update_pending;

}

esp_err_t ota_process_start(void){

    xSemaphoreGive(ota_service_state.start_update);
    return ESP_OK;
}



esp_err_t ota_set_valid(bool valid){
    if(valid){
        esp_ota_mark_app_valid_cancel_rollback();
        //Update maybe pending if previous was stopped because of an earlier verification pending
        if(update_pending())
            ota_process_start();
    }
    else
        esp_ota_mark_app_invalid_rollback_and_reboot();
    
    return ESP_OK;
}


static void set_fixed_time_for_tls(void){
    struct timeval tv = {
        .tv_sec = 1760000000,  // Oct 2025 (inside GitHub cert validity)
        .tv_usec = 0
    };

    settimeofday(&tv, NULL);

    time_t now;
    time(&now);
    ESP_LOGE("TLS_TIME", "System time set to: %lld", now);
}



esp_err_t ota_service_init(){
    ESP_LOGI(TAG, "OTA example app_main start");

esp_http_client_config_t config={0};

    config.url = "http://example.com";    //Some random address which will be replaced before request
    config.cert_pem = (char *)server_cert_pem_start;
    config.timeout_ms = OTA_RECV_TIMEOUT;
    config.keep_alive_enable = true;
    config.buffer_size = 2048;    // Explicity supplied large value instead of not setting it and thus using default size, because github is sending a big header in responce which contains redirect url
    config.buffer_size_tx = 2048;  // request side can stay small
    config.disable_auto_redirect=true;
    config.event_handler=_http_event_handler;
    config.user_data=&ota_service_state;


    esp_http_client_handle_t* client = &ota_service_state.client;
    *client = esp_http_client_init(&config);
    if (*client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        return ERR_OTA_SERVICE_INIT_FAIL;
    }
 

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
            OTA_SERVICE_post_event(OTA_SERVICE_ROUTINE_EVENT_VERIFICATION_PENDING,NULL,0);
            return ERR_OTA_SERVICE_VALIDATION_PENDING;
        }
    }


    set_fixed_time_for_tls();



    
 



    OTA_SERVICE_register_event(OTA_SERVICE_ROUTINE_EVENT_REBOOT_REQUIRED,NULL,NULL);
    OTA_SERVICE_register_event(OTA_SERVICE_ROUTINE_EVENT_VERIFICATION_PENDING,NULL,NULL);

    


    //send_update_process_start_signal();
    return ESP_OK;
    
}