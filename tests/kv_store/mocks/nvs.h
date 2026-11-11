/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_KV_STORE_MOCKS_NVS_H_
#define MYBOT_TEST_KV_STORE_MOCKS_NVS_H_

#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
typedef unsigned nvs_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_TYPE_MISMATCH 0x1103
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE 0x1105
#define ESP_ERR_NVS_INVALID_LENGTH 0x110c
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#define NVS_READWRITE 1
typedef struct {
    size_t used_entries, free_entries, available_entries, total_entries, namespace_count;
} nvs_stats_t;
const char *esp_err_to_name(esp_err_t error);
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
esp_err_t nvs_get_stats(const char *partition, nvs_stats_t *stats);
void nvs_close(nvs_handle_t handle);

#endif /* MYBOT_TEST_KV_STORE_MOCKS_NVS_H_ */
