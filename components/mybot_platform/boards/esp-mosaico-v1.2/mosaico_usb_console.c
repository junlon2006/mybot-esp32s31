/* SPDX-License-Identifier: Apache-2.0 */
#include "mosaico_usb_console.h"

#include <stdbool.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"
#include "tinyusb_default_config.h"
#include "usb/mosaico_usb_audio.h"
#include "usb/mosaico_usb_descriptors.h"

static bool s_audio_initialized;

bool mosaico_usb_audio_initialized(void) {
    return s_audio_initialized;
}

static void log_usb_status(void *arg) {
    (void)arg;
    mosaico_usb_audio_log_stats();
}

esp_err_t mosaico_usb_console_init(void) {
    static bool attempted;
    static esp_err_t result = ESP_ERR_INVALID_STATE;
    if (attempted)
        return result;
    attempted = true;
    /* One owner for the native HS UTMI PHY, USB task and both classes. */
    tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG(mosaico_usb_audio_event, NULL);
    config.task.xCoreID = 0;
    result = mybot_mosaico_usb_descriptors_configure(&config);
    if (result != ESP_OK)
        return result;
    /* The host can select an audio interface as soon as the stack is exposed.
     * Prepare UAC workers first; the pinned component has no teardown API. */
    result = mosaico_usb_audio_prepare();
    if (result != ESP_OK)
        return result;
    result = tinyusb_driver_install(&config);
    if (result != ESP_OK)
        return result;
    const tinyusb_config_cdcacm_t cdc = {.cdc_port = TINYUSB_CDC_ACM_0};
    result = tinyusb_cdcacm_init(&cdc);
    if (result != ESP_OK) {
        (void)tinyusb_driver_uninstall();
        return result;
    }
    s_audio_initialized = true;
    /* The upstream CDC VFS writes nonblocking: unplugging a USB cable must not
     * block audio, networking or translation. Console ownership lasts to reboot. */
    result = tinyusb_console_init(TINYUSB_CDC_ACM_0);
    if (result == ESP_OK) {
        ESP_LOGI("mosaico", "event=console_ready transport=usb_cdc audio=uac2");
        /* ESP_TIMER_TASK reports counters outside the USB task/ISR. CDC VFS
         * writes are nonblocking, including when no terminal is attached. */
        static esp_timer_handle_t status_timer;
        const esp_timer_create_args_t timer_config = {
            .callback = log_usb_status,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "mosaico_usb",
            .skip_unhandled_events = true,
        };
        esp_err_t timer_result = esp_timer_create(&timer_config, &status_timer);
        if (timer_result == ESP_OK) {
            timer_result = esp_timer_start_periodic(status_timer, 5000000);
            if (timer_result != ESP_OK) {
                (void)esp_timer_delete(status_timer);
                status_timer = NULL;
            }
        }
        if (timer_result != ESP_OK)
            ESP_LOGW("mosaico", "event=usb_status_timer result=error error=%s",
                     esp_err_to_name(timer_result));
    }
    return result;
}
