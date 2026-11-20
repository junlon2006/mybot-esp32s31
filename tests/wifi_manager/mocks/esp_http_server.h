/* SPDX-License-Identifier: MIT */
#ifndef TEST_WIFI_MANAGER_ESP_HTTP_SERVER_H
#define TEST_WIFI_MANAGER_ESP_HTTP_SERVER_H
#include "esp_err.h"
#include <cstddef>
#define ESP_FAIL -1
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_SOCK_ERR_TIMEOUT -3
typedef void *httpd_handle_t;
struct httpd_req_t {
    size_t content_len;
    void *user_ctx;
};
int httpd_req_recv(httpd_req_t *, char *, size_t);
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *, const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_status(httpd_req_t *, const char *);
esp_err_t httpd_resp_sendstr(httpd_req_t *, const char *);
esp_err_t httpd_resp_send_408(httpd_req_t *);
esp_err_t httpd_resp_send_err(httpd_req_t *, int, const char *);
#endif
