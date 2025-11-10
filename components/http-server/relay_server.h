#ifndef RELAY_SERVER_H
#define RELAY_SERVER_H

#include "esp_err.h"


//fwd  decleration. So that the orginal data struucture httpd_req_t is hidden from user
/// and esp_http_server is a PRIV_REQUIRE
typedef struct http_request_t http_request_t;


//Through this the user will be informed about the request
//The req here is just a context pointer which will be sent back in the reply
typedef void (*request_callback)(http_request_t* req,const char* uri);


typedef enum {
    METHOD_GET,
    METHOD_POST
} request_method_t;

typedef struct {
    
    esp_err_t (*register_uri)(const char* uri,request_method_t method,request_callback cb);
    //When it is desired to reply with a text
    esp_err_t (*send_response)(http_request_t* req,const char* buff);
    //When it is desired to reply with error. Right now only error is "Uri not found etc"
    esp_err_t (*send_error_response)(http_request_t* req,const char* message);

    esp_err_t (*close_async_connection)(http_request_t* req);
}relay_server_interface_t;



typedef enum {
    PROTOCOL_HTTP,          // Regular HTTP
    PROTOCOL_HTTPS          // Secure HTTPS
} server_protocol_t;

typedef struct {
    uint16_t port;                    // Which port to listen on (default: 80 for HTTP, 443 for HTTPS)
    server_protocol_t protocol;      // HTTP or HTTPS
    uint8_t max_uris;                // How many different URIs to support (default: 10)
    uint16_t max_connections;        // Max simultaneous clients (default: 4)

} relay_server_config_t;






relay_server_interface_t* relay_server_init(relay_server_config_t* config);







#endif