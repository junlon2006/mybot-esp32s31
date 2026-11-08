/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_platform/factory_reset.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include <stdint.h>

#define TAG "mybot_factory_reset"
#define FACTORY_RESET_MAGIC UINT32_C(0x4d594252)
#define FACTORY_RESET_VERSION UINT32_C(1)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t inverse;
} factory_reset_marker_t;

RTC_NOINIT_ATTR static factory_reset_marker_t s_marker;

static bool marker_is_pending(void) {
    return s_marker.magic == FACTORY_RESET_MAGIC && s_marker.version == FACTORY_RESET_VERSION &&
           s_marker.inverse == ~FACTORY_RESET_MAGIC;
}

static void marker_set(void) {
    /* Clear first so a torn update cannot leave a valid-looking old marker. */
    s_marker.magic = 0;
    s_marker.version = FACTORY_RESET_VERSION;
    s_marker.inverse = ~FACTORY_RESET_MAGIC;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    s_marker.magic = FACTORY_RESET_MAGIC;
}

static void marker_clear(void) {
    s_marker.magic = 0;
    s_marker.version = 0;
    s_marker.inverse = 0;
    __atomic_thread_fence(__ATOMIC_RELEASE);
}

void mybot_factory_reset_request(void) {
    marker_set();
    ESP_LOGW(TAG, "event=factory_reset action=request result=reboot");
    esp_restart();
}

bool mybot_factory_reset_handle_pending(void) {
    if (!marker_is_pending()) {
        return false;
    }

    esp_err_t result = nvs_flash_erase();
    if (result == ESP_OK) {
        marker_clear();
        ESP_LOGI(TAG, "event=factory_reset action=erase_nvs result=ok action=reboot");
    } else {
        ESP_LOGE(TAG,
                 "event=factory_reset action=erase_nvs result=error error=%s "
                 "marker=retained action=reboot_retry",
                 esp_err_to_name(result));
    }
    esp_restart();
    return true;
}
