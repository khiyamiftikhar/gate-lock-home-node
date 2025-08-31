#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include "relay_server.h"

#define         MAX_URIS                    5
#define         MAX_URI_LENGTH              15

typedef struct {
    char uri[MAX_URI_LENGTH];           // Points to user's string literal
    request_callback callback;
    request_method_t method;     // HTTP_GET, HTTP_POST, etc.
} relay_uri_record_t;



//This is to encapsulate the httpd_req_t type so that esp_http_server is in PRIV_REQUIRES
//struct http_request {
  //  httpd_req_t *req;   // keep the original context
//};

static struct{
    httpd_handle_t server_handle;
    relay_server_interface_t interface;
    //request_callback cb;
    relay_uri_record_t uri_record[MAX_URIS];
    int uri_count;
}relay_server={0};
  


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


static esp_err_t relay_server_send_response(http_request_t* req, 
                                   const char* data) {
    if (!req || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    httpd_req_t* request=(httpd_req_t*)req;
    // Set default headers
    esp_err_t ret = httpd_resp_set_type(request, "text/plain");
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Send response with specified length
    return httpd_resp_send(request, data, strlen(data));
}

static esp_err_t relay_server_send_error(http_request_t* req, 
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



static esp_err_t relay_server_send_html_response(httpd_req_t* req,
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

static esp_err_t relay_server_send_chunk(httpd_req_t* req,
                                const char* chunk_data,
                                size_t len) {
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // For streaming responses
    return httpd_resp_send_chunk(req, chunk_data, len);
}

static esp_err_t relay_server_end_response(httpd_req_t* req) {
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // End chunked response
    return httpd_resp_send_chunk(req, NULL, 0);
}


static esp_err_t relay_server_send_status_error(httpd_req_t* req,
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


static esp_err_t master_request_handler(httpd_req_t *req){
    // Extract server context from ESP-IDF user_ctx
    
    // Linear search through registered URIs
    for (size_t i = 0; i < relay_server.uri_count; i++) {
        if (strcmp(relay_server.uri_record[i].uri, req->uri) == 0) {

            ESP_LOGI(TAG,"record i %d, uri %s",i,req->uri);
            // Found matching URI, call user callback
            relay_server.uri_record[i].callback((http_request_t*)req, req->uri);
            return ESP_OK;
        }
    }
    
    // No handler found
    //http_request_t request={.req=req};
    relay_server_send_error((http_request_t*)req, "404 Not Found");
    return ESP_OK;
}


static esp_err_t relay_server_register_uri(const char* uri,request_method_t method,request_callback cb){
    if(uri==NULL || cb==NULL || strlen(uri)>MAX_URI_LENGTH)
        return ESP_ERR_INVALID_ARG;

    int uri_count=relay_server.uri_count;
    relay_server.uri_record[uri_count].callback=cb;
    memcpy(relay_server.uri_record[uri_count].uri,uri,strlen(uri));
    relay_server.uri_record[uri_count].method=method;

    
    ESP_LOGI(TAG,"uri %s",relay_server.uri_record[uri_count].uri);
    // Register with ESP-IDF HTTP server
    httpd_uri_t esp_uri = {
        .uri = relay_server.uri_record[uri_count].uri,
        .method = HTTP_GET,     //The supplied methid not used as it requires enum type translation
        .handler = master_request_handler,
        .user_ctx = NULL  // Pass our server context
    };


//    cb(NULL,"/close-gate");
  //  cb(NULL,"/open-gate");
    relay_server.uri_count++;
    
    return httpd_register_uri_handler(relay_server.server_handle, &esp_uri);
}




relay_server_interface_t* relay_server_init(relay_server_config_t* config){

    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.max_open_sockets = config->max_connections;
    http_config.uri_match_fn = httpd_uri_match_wildcard;
    http_config.server_port = config->port;
    

    relay_server.interface.register_uri=relay_server_register_uri;
    relay_server.interface.send_response=relay_server_send_response;
    relay_server.interface.send_error_response=relay_server_send_error;

    ESP_LOGI(TAG, "Starting HTTP Server");
    if( httpd_start(&relay_server.server_handle, &http_config) != ESP_OK){

    
        ESP_LOGI(TAG,"Server Init Failed");
        return NULL;
    }

    return &relay_server.interface;


}



esp_err_t stopHttpServer(){
    return httpd_stop(relay_server.server_handle);

}
