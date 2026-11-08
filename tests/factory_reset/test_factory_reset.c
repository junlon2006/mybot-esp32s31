/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_platform/factory_reset.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static esp_err_t erase_result;
static unsigned erase_calls, restart_calls, log_calls;
static char last_log[256];

esp_err_t nvs_flash_erase(void) {
    ++erase_calls;
    return erase_result;
}

void esp_restart(void) {
    ++restart_calls;
}

const char *esp_err_to_name(esp_err_t result) {
    return result == ESP_OK ? "ESP_OK" : "ESP_FAIL";
}

void test_factory_reset_log(const char *tag, const char *format, ...) {
    assert(tag && !strcmp(tag, "mybot_factory_reset"));
    va_list args;
    va_start(args, format);
    vsnprintf(last_log, sizeof(last_log), format, args);
    va_end(args);
    ++log_calls;
}

int main(void) {
    assert(!mybot_factory_reset_handle_pending());
    assert(erase_calls == 0 && restart_calls == 0 && log_calls == 0);

    mybot_factory_reset_request();
    assert(restart_calls == 1 && strstr(last_log, "action=request"));
    assert(mybot_factory_reset_handle_pending());
    assert(erase_calls == 1 && restart_calls == 2);
    assert(strstr(last_log, "action=erase_nvs result=ok"));
    assert(!mybot_factory_reset_handle_pending());
    assert(erase_calls == 1 && restart_calls == 2);

    erase_result = ESP_FAIL;
    mybot_factory_reset_request();
    assert(mybot_factory_reset_handle_pending());
    assert(erase_calls == 2 && restart_calls == 4);
    assert(strstr(last_log, "marker=retained"));
    assert(mybot_factory_reset_handle_pending());
    assert(erase_calls == 3 && restart_calls == 5);
    erase_result = ESP_OK;
    assert(mybot_factory_reset_handle_pending());
    assert(erase_calls == 4 && restart_calls == 6);
    assert(!mybot_factory_reset_handle_pending());

    puts("PASS factory reset marker, erase retry and restart sequencing");
}
