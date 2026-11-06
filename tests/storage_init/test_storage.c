/* SPDX-License-Identifier: Apache-2.0 */
#include "storage_init.h"
#include "nvs_flash.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static esp_err_t init_result;
static unsigned init_calls, erase_calls, log_calls;
static unsigned char stored_data[] = {0x17, 0x29, 0x42};
static char last_log[256];

esp_err_t nvs_flash_init(void) {
    ++init_calls;
    return init_result;
}
esp_err_t nvs_flash_erase(void) {
    ++erase_calls;
    memset(stored_data, 0xff, sizeof(stored_data));
    return ESP_OK;
}
const char *esp_err_to_name(esp_err_t result) {
    switch (result) {
    case ESP_ERR_NVS_NO_FREE_PAGES:
        return "ESP_ERR_NVS_NO_FREE_PAGES";
    case ESP_ERR_NVS_NEW_VERSION_FOUND:
        return "ESP_ERR_NVS_NEW_VERSION_FOUND";
    case ESP_ERR_NO_MEM:
        return "ESP_ERR_NO_MEM";
    default:
        return "ESP_FAIL";
    }
}
void test_storage_log(const char *tag, const char *format, ...) {
    assert(!strcmp(tag, "mybot_storage"));
    va_list args;
    va_start(args, format);
    vsnprintf(last_log, sizeof(last_log), format, args);
    va_end(args);
    ++log_calls;
}
int main(void) {
    const unsigned char expected[] = {0x17, 0x29, 0x42};
    const esp_err_t results[] = {ESP_OK, ESP_ERR_NVS_NO_FREE_PAGES, ESP_ERR_NVS_NEW_VERSION_FOUND,
                                 ESP_ERR_NO_MEM, ESP_FAIL};
    for (unsigned i = 0; i < sizeof(results) / sizeof(results[0]); ++i) {
        init_result = results[i];
        init_calls = erase_calls = log_calls = 0;
        last_log[0] = '\0';
        assert(mybot_storage_init() == (init_result == ESP_OK ? 0 : -1));
        assert(init_calls == 1 && erase_calls == 0);
        assert(!memcmp(stored_data, expected, sizeof(expected)));
        assert(log_calls == (init_result == ESP_OK ? 0U : 1U));
        if (init_result != ESP_OK) {
            assert(strstr(last_log, esp_err_to_name(init_result)));
            assert(strstr(last_log, "data=preserved"));
        }
    }
    puts("PASS storage initialization preserves data for all failure paths");
}
