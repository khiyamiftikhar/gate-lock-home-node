#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include "http_server.h"

#define         MAX_URIS                    5
#define         MAX_URI_LENGTH              15

typedef struct {
    char uri[MAX_URI_LENGTH];           // Points to user's string literal
    request_callback callback;
    request_method_t method;     // HTTP_GET, HTTP_POST, etc.
} http_uri_record_t;


// Static pool
#define MAX_ASYNC_REQUESTS 4

typedef struct {
    httpd_req_t *req;   // pointer to async request
    bool in_use;
} async_slot_t;

// Global pool
static async_slot_t async_pool[MAX_ASYNC_REQUESTS];


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


static async_slot_t* async_slot_allocate(httpd_req_t *req)
{
    xSemaphoreTake(http_server.pool_mutex, portMAX_DELAY);
    async_slot_t *slot = NULL;
    for (int i = 0; i < MAX_ASYNC_REQUESTS; i++) {
        if (!async_pool[i].in_use) {
            async_pool[i].req = req;
            async_pool[i].in_use = true;
            slot = &async_pool[i];
            ESP_LOGI(TAG, "Allocated slot %d for req %p", i, req);
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
            ESP_LOGI(TAG, "Freed slot %d for req %p", i, req);
            break;
        }
    }
    xSemaphoreGive(http_server.pool_mutex);
}


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

static esp_err_t http_server_send_chunk(httpd_req_t* req,
                                const char* chunk_data,
                                size_t len) {
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // For streaming responses
    return httpd_resp_send_chunk(req, chunk_data, len);
}

static esp_err_t http_server_end_response(httpd_req_t* req) {
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // End chunked response
    return httpd_resp_send_chunk(req, NULL, 0);
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
    

    ESP_LOGI(TAG, "Completing async request: %p", asyn_request);
    esp_err_t res = httpd_req_async_handler_complete(request);
    ESP_LOGI(TAG, "Complete result: %s", esp_err_to_name(res));


    async_slot_free(asyn_request->req);
    return ret;

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
            async_slot_t* async_slot = async_slot_allocate(async_req);
            if(async_slot==NULL){
                http_server_send_status_error(req,
                                       503,
                                       "Server Busy"); 
                httpd_req_async_handler_complete(req);
                return ESP_OK;
            }
            
            

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

    ESP_LOGI(TAG, "Starting HTTP Server");
    if( httpd_start(&http_server.server_handle, &http_config) != ESP_OK){

    
        ESP_LOGI(TAG,"Server Init Failed");
        return ESP_FAIL;
    }

    http_server.pool_mutex = xSemaphoreCreateMutex();
    for (int i = 0; i < MAX_ASYNC_REQUESTS; i++) {
        async_pool[i].req = NULL;
        async_pool[i].in_use = false;
    }

    return ESP_OK;
}



esp_err_t stopHttpServer(){
    return httpd_stop(http_server.server_handle);
}
