#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"
#include <esp_http_server.h>
#include "esp_event.h"
#include "esp_netif.h"

static const char *TAG = "http";
static const char *page_content = NULL;

static esp_err_t hello_get_handler(httpd_req_t *req)
{
    httpd_resp_send(req, page_content, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* A HTTP GET handler */
static esp_err_t redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");

    const char* resp_str = "Redirecting...";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static httpd_uri_t gen204_route = {
    .uri       = "/generate_204",
    .method    = HTTP_GET,
    .handler   = redirect_handler
};

static httpd_uri_t root_route = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = hello_get_handler
};

static httpd_handle_t server = NULL;
void start_http_server(const char *html_page)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    page_content = html_page;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root_route);
        httpd_register_uri_handler(server, &gen204_route);
    } else {
        ESP_LOGI(TAG, "Error starting server!");
    }
}

esp_err_t stop_webserver()
{
    // Stop the httpd server
    return httpd_stop(server);
}