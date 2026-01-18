#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include "bank_pool.h"  
#include "http_server.h"

#define         MAX_URIS                    5
#define         MAX_URI_LENGTH              15

#define LOG_CHUNK_SIZE   256



typedef struct {
    bool   is_last;
    size_t len;
    char   data[LOG_CHUNK_SIZE];
} http_chunk_t;

typedef struct {
    char uri[MAX_URI_LENGTH];           // Points to user's string literal
    request_callback callback;
    request_method_t method;     // HTTP_GET, HTTP_POST, etc.
} http_uri_record_t;


// Static pool

#define MAX_ASYNC_REQUESTS 4

typedef struct {
    httpd_req_t *req;
    bool response_started;
} async_slot_t;


typedef struct {
    async_slot_t *slot;
    http_chunk_t *chunk;
} http_send_job_t;


#define LOG_CHUNK_POOL     4
#define MAX_ASYNC_REQUESTS 4
#define SEND_JOB_POOL      4   // usually same as chunk pool

static http_chunk_t    g_chunk_objs[LOG_CHUNK_POOL];
static bank_pool_handle_t g_chunk_bank;

static async_slot_t    g_async_objs[MAX_ASYNC_REQUESTS];
static bank_pool_handle_t g_async_bank;

static http_send_job_t g_job_objs[SEND_JOB_POOL];
static bank_pool_handle_t g_job_bank;

//This is to encapsulate the httpd_req_t type so that esp_http_server is in PRIV_REQUIRES
//struct http_request {
  //  httpd_req_t *req;   // keep the original context
//};

static struct{
    httpd_handle_t server_handle;
    http_server_interface_t interface;
    //request_callback cb;
    http_uri_record_t uri_record[MAX_URIS];
    SemaphoreHandle_t pool_mutex;
    int uri_count;
}http_server={0};
  


static const char *TAG = "HTTP Server";



#define REST_CHECK(a, str, goto_tag, ...)                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(a))                                                                      \
        {                                                                              \
            ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                             \
        }                                                                              \
    } while (0)


/*
static async_slot_t* async_slot_allocate(httpd_req_t *req)
{
    xSemaphoreTake(http_server.pool_mutex, portMAX_DELAY);
    async_slot_t *slot = NULL;
    for (int i = 0; i < MAX_ASYNC_REQUESTS; i++) {
        if (!async_pool[i].in_use) {
            async_pool[i].req = req;
            async_pool[i].in_use = true;
            async_pool[i].close_requested = false;
            async_pool[i].response_started=false;
            slot = &async_pool[i];
            ESP_LOGI(TAG, "Allocated slot %d and pointer %p for req %p", i, slot, req);
            break;
        }
    }
    xSemaphoreGive(http_server.pool_mutex);

    
    return slot;  // NULL if no free slot
}

static void async_slot_free(httpd_req_t *req)
{
    xSemaphoreTake(http_server.pool_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_ASYNC_REQUESTS; i++) {
        if (async_pool[i].in_use && async_pool[i].req == req) {
            async_pool[i].req = NULL;
            async_pool[i].in_use = false;
            async_pool[i].response_started=false;
            ESP_LOGI(TAG, "Freed slot %d for req %p", i, req);
            break;
        }
    }
    xSemaphoreGive(http_server.pool_mutex);
}

static http_chunk_t *chunk_alloc(void)
{
    if (xSemaphoreTake(g_chunk_pool.mutex, 0) != pdTRUE) {
        return NULL;
    }

    for (int i = 0; i < LOG_CHUNK_POOL; i++) {
        if (!g_chunk_pool.chunks[i].in_use) {
            g_chunk_pool.chunks[i].in_use = true;
            xSemaphoreGive(g_chunk_pool.mutex);
            return &g_chunk_pool.chunks[i];
        }
    }

    xSemaphoreGive(g_chunk_pool.mutex);
    return NULL;
}

static void chunk_free(http_chunk_t *chunk)
{
    xSemaphoreTake(g_chunk_pool.mutex, portMAX_DELAY);
    chunk->in_use = false;
    xSemaphoreGive(g_chunk_pool.mutex);
}


*/

static esp_err_t http_server_send_response(http_request_t* req, const char* data) {
    if (!req || !data) return ESP_ERR_INVALID_ARG;

    async_slot_t* asyn_request = (async_slot_t*)req;
    httpd_req_t* request = asyn_request->req;

    const char* uri = request->uri;   // <-- direct access

    char resp[100];   // no malloc
    httpd_resp_set_type(request, "text/plain");
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(request, "Connection", "close");

    httpd_resp_send_chunk(request, request->uri, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(request, ": ", 2);
    httpd_resp_send_chunk(request, data, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(request, NULL, 0);  // end

    return ESP_OK;
}






static esp_err_t http_server_send_error(http_request_t* req, 
                                const char* error_msg) {
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_req_t* request=(httpd_req_t*)req;
    
    esp_err_t ret = httpd_resp_set_status(request, "404 Not Found");
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = httpd_resp_set_type(request, "text/plain");
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    if (ret != ESP_OK) {
        return ret;
    }
    
    const char* msg = error_msg ? error_msg : "Resource not found";
    return httpd_resp_send(request, msg, strlen(msg));
}


//These one below nnot used/included in interface yet
static esp_err_t relay_server_send_json_response(httpd_req_t* req,
                                        const char* json_data,
                                        size_t len) {
    if (!req || !json_data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = httpd_resp_set_type(req, "application/json");
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    if (ret != ESP_OK) {
        return ret;
    }
    
    return httpd_resp_send(req, json_data, len);
}



static esp_err_t http_server_send_html_response(httpd_req_t* req,
                                        const char* html_data,
                                        size_t len) {
    if (!req || !html_data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = httpd_resp_set_type(req, "text/html; charset=utf-8");
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    if (ret != ESP_OK) {
        return ret;
    }
    
    return httpd_resp_send(req, html_data, len);
}


static esp_err_t http_server_send_status_error(httpd_req_t* req,
                                       int status_code,
                                       const char* error_msg) {
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Convert status code to string
    char status_str[32];
    const char* status_text;
    
    switch (status_code) {
        case 400: status_text = "400 Bad Request"; break;
        case 401: status_text = "401 Unauthorized"; break;
        case 403: status_text = "403 Forbidden"; break;
        case 404: status_text = "404 Not Found"; break;
        case 405: status_text = "405 Method Not Allowed"; break;
        case 500: status_text = "500 Internal Server Error"; break;
        case 503: status_text = "503 Service Unavailable"; break;
        default:
            snprintf(status_str, sizeof(status_str), "%d Error", status_code);
            status_text = status_str;
            break;
    }
    
    esp_err_t ret = httpd_resp_set_status(req, status_text);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = httpd_resp_set_type(req, "text/plain");
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    if (ret != ESP_OK) {
        return ret;
    }
    
    const char* msg = error_msg ? error_msg : "An error occurred";
    return httpd_resp_send(req, msg, strlen(msg));
}

esp_err_t http_server_close_async_connection(http_request_t *req){

    esp_err_t ret=0;
    async_slot_t* asyn_request=(async_slot_t*)req;

    httpd_req_t* request=asyn_request->req;
    

    ESP_LOGI(TAG, "Completing async request: %p, for req , %p", asyn_request, asyn_request->req);

    esp_err_t res = httpd_req_async_handler_complete(request);
    ESP_LOGI(TAG, "Complete result: %s", esp_err_to_name(res));


    bank_free(g_async_bank, (void*)asyn_request);
    return ret;

}

/*
static void http_async_close_worker(void *arg)
{
    async_slot_t *slot = arg;

    ESP_LOGI(TAG, "Closing worker: %p", slot);
    if (!slot->in_use || !slot->close_requested) {
        return;
    }

    http_server_close_async_connection((http_request_t *)slot);
    slot->in_use = false;
}
*/

static void http_async_send_worker(void *arg)
{
    http_send_job_t *job = arg;
    async_slot_t *slot = job->slot;
    http_chunk_t *chunk = job->chunk;
    httpd_req_t *req = slot->req;

    if (!slot->response_started) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        httpd_resp_set_hdr(req, "Connection", "close");
        slot->response_started = true;
    }

    if (chunk->is_last) {
        httpd_resp_send_chunk(req, NULL, 0);
        http_server_close_async_connection((http_request_t *)slot);

        bank_free(g_chunk_bank, chunk);
        //bank_free(g_async_bank, slot);
        bank_free(g_job_bank, job);
        return;
    }

    httpd_resp_send_chunk(req, chunk->data, chunk->len);

    bank_free(g_chunk_bank, chunk);
    bank_free(g_job_bank, job);
}


esp_err_t http_server_send_chunked_response(http_request_t *req,
                                            const char *data)
{
    async_slot_t *slot = (async_slot_t *)req;

    
    http_chunk_t *chunk = bank_alloc(g_chunk_bank);
    if (!chunk) {
        ESP_LOGE(TAG, "Failed to allocate chunk");
        return ESP_OK;   // best effort
    }

    http_send_job_t *job = bank_alloc(g_job_bank);
    if (!job) {
        ESP_LOGE(TAG, "Failed to allocate send job");
        bank_free(g_chunk_bank, chunk);
        return ESP_OK;   // best effort
    }

    if (data) {
        strncpy(chunk->data, data, LOG_CHUNK_SIZE);
        chunk->len = strnlen(chunk->data, LOG_CHUNK_SIZE);
        chunk->is_last = false;
    } else {
        chunk->len = 0;
        chunk->is_last = true;
    }

    job->slot  = slot;
    job->chunk = chunk;

    httpd_queue_work(http_server.server_handle,
                     http_async_send_worker,
                     job);

    return ESP_OK;
}



static esp_err_t master_request_handler(httpd_req_t *req){
    // Extract server context from ESP-IDF user_ctx
   
    httpd_req_t *async_req;

    
    
    
    // Linear search through registered URIs
    for (size_t i = 0; i < http_server.uri_count; i++) {
        if (strcmp(http_server.uri_record[i].uri, req->uri) == 0) {

            ESP_LOGI(TAG,"record i %d, uri %s",i,req->uri);
            // Found matching URI, call user callback


            httpd_req_async_handler_begin(req, &async_req);
            async_slot_t* async_slot =(async_slot_t*) bank_alloc(g_async_bank);
            
            ESP_LOGI(TAG, "Allocated async slot %p for req %p", async_slot, req);
            if(async_slot==NULL){
                http_server_send_status_error(req,
                                       503,
                                       "Server Busy"); 
                httpd_req_async_handler_complete(req);
                return ESP_OK;
            }
            
            async_slot->req=async_req;
            //call the corresponding callback registered by the user_request 
            http_server.uri_record[i].callback((http_request_t*)async_slot, async_req->uri);
            return ESP_OK;
        }
    }
    
    // No handler found
    //http_request_t request={.req=req};
    http_server_send_error((http_request_t*)req, "404 Not Found");
    return ESP_OK;
}


static esp_err_t http_server_register_uri(const char* uri,request_method_t method,request_callback cb){
    if(uri==NULL || cb==NULL || strlen(uri)>MAX_URI_LENGTH)
        return ESP_ERR_INVALID_ARG;

    int uri_count=http_server.uri_count;
    http_server.uri_record[uri_count].callback=cb;
    memcpy(http_server.uri_record[uri_count].uri,uri,strlen(uri));
    http_server.uri_record[uri_count].method=method;

    
    ESP_LOGI(TAG,"uri %s",http_server.uri_record[uri_count].uri);
    // Register with ESP-IDF HTTP server
    httpd_uri_t esp_uri = {
        .uri = http_server.uri_record[uri_count].uri,
        .method = HTTP_GET,     //The supplied methid not used as it requires enum type translation
        .handler = master_request_handler,
        .user_ctx = NULL  // Pass our server context
    };


//    cb(NULL,"/close-gate");
  //  cb(NULL,"/open-gate");
    http_server.uri_count++;
    return httpd_register_uri_handler(http_server.server_handle, &esp_uri);
}


http_server_interface_t* http_server_get_interface(){
    if(http_server.server_handle==NULL)
        return NULL;
    return &http_server.interface;
}



esp_err_t http_server_init(http_server_config_t* config){

    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.max_open_sockets = config->max_connections;
    http_config.uri_match_fn = httpd_uri_match_wildcard;
    http_config.server_port = config->port;
    

    http_server.interface.register_uri=http_server_register_uri;
    http_server.interface.send_response=http_server_send_response;
    http_server.interface.send_error_response=http_server_send_error;
    http_server.interface.close_async_connection=http_server_close_async_connection;
    http_server.interface.send_chunked_response=http_server_send_chunked_response; 
    

    ESP_LOGI(TAG, "Starting HTTP Server");
    if( httpd_start(&http_server.server_handle, &http_config) != ESP_OK){

    
        ESP_LOGI(TAG,"Server Init Failed");
        return ESP_FAIL;
    }

    http_server.pool_mutex = xSemaphoreCreateMutex();

    bank_register_pool(&g_chunk_bank,
                       g_chunk_objs,
                       sizeof(http_chunk_t),
                       LOG_CHUNK_POOL
                       );

    bank_register_pool(&g_async_bank,
                       g_async_objs,
                       sizeof(async_slot_t),
                       MAX_ASYNC_REQUESTS);

    bank_register_pool(&g_job_bank,
                       g_job_objs,
                       sizeof(http_send_job_t),
                       SEND_JOB_POOL);


    return ESP_OK;
}



esp_err_t stopHttpServer(){
    return httpd_stop(http_server.server_handle);
}
