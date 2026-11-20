/* SPDX-License-Identifier: MIT */
#ifndef TEST_WIFI_MANAGER_NVS_H
#define TEST_WIFI_MANAGER_NVS_H
#include "esp_err.h"
#include <cstddef>
using nvs_handle_t = int;
enum { NVS_READONLY, NVS_READWRITE, ESP_ERR_NVS_NOT_FOUND = 12 };
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
void nvs_close(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_get_str(nvs_handle_t, const char *, char *, size_t *);
esp_err_t nvs_commit(nvs_handle_t);
#endif
