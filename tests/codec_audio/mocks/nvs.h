/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_CODEC_AUDIO_MOCKS_NVS_H_
#define MYBOT_TEST_CODEC_AUDIO_MOCKS_NVS_H_

#include "esp_err.h"
#include <stdint.h>
typedef int nvs_handle_t;
#define NVS_READWRITE 1
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_get_i32(nvs_handle_t, const char *, int32_t *);
esp_err_t nvs_set_i32(nvs_handle_t, const char *, int32_t);
esp_err_t nvs_erase_key(nvs_handle_t, const char *);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);

#endif /* MYBOT_TEST_CODEC_AUDIO_MOCKS_NVS_H_ */
