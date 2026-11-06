/* SPDX-License-Identifier: Apache-2.0 */
#include "mosaico_usb_console.h"
#include "usb/mosaico_usb_audio.h"
#include "usb/mosaico_usb_descriptors.h"
#include "esp_timer.h"
#include "tinyusb_cdc_acm.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { DESCRIPTORS, AUDIO, CONTROLLER, CDC_CLASS, CONSOLE, STAGE_COUNT };
static const char *scenario;
static unsigned calls[STAGE_COUNT], uninstalls, timer_creates, timer_starts, timer_deletes;
static unsigned status_logs, events;
static bool installed;
static tinyusb_config_t installed_config;
static esp_timer_create_args_t status_timer;

static bool fails(const char *name) {
    return !strcmp(scenario, name);
}

void test_usb_init_log(const char *tag, const char *format, ...) {
    assert(tag && format);
}

const char *esp_err_to_name(esp_err_t error) {
    assert(error == ESP_FAIL);
    return "ESP_FAIL";
}

esp_err_t mybot_mosaico_usb_descriptors_configure(tinyusb_config_t *config) {
    assert(config && !calls[AUDIO] && !mosaico_usb_audio_initialized());
    ++calls[DESCRIPTORS];
    config->descriptors_configured = true;
    return fails("descriptors") ? ESP_FAIL : ESP_OK;
}

esp_err_t mosaico_usb_audio_prepare(void) {
    assert(calls[DESCRIPTORS] == 1 && !calls[CONTROLLER] && !mosaico_usb_audio_initialized());
    ++calls[AUDIO];
    return fails("audio") ? ESP_FAIL : ESP_OK;
}

esp_err_t tinyusb_driver_install(const tinyusb_config_t *config) {
    assert(calls[AUDIO] == 1 && config->descriptors_configured && !calls[CDC_CLASS]);
    assert(config->task.xCoreID == 0 && config->event_cb == mosaico_usb_audio_event);
    assert(!mosaico_usb_audio_initialized());
    ++calls[CONTROLLER];
    if (fails("controller"))
        return ESP_FAIL;
    installed = true;
    installed_config = *config;
    return ESP_OK;
}

esp_err_t tinyusb_driver_uninstall(void) {
    assert(installed && calls[CDC_CLASS] == 1 && !calls[CONSOLE]);
    ++uninstalls;
    if (fails("cdc_uninstall"))
        return ESP_FAIL;
    installed = false;
    return ESP_OK;
}

esp_err_t tinyusb_cdcacm_init(const tinyusb_config_cdcacm_t *config) {
    assert(installed && config->cdc_port == TINYUSB_CDC_ACM_0);
    assert(!mosaico_usb_audio_initialized());
    ++calls[CDC_CLASS];
    return fails("cdc_class") || fails("cdc_uninstall") ? ESP_FAIL : ESP_OK;
}

esp_err_t tinyusb_console_init(int cdc_port) {
    assert(installed && cdc_port == TINYUSB_CDC_ACM_0 && calls[CDC_CLASS] == 1);
    assert(mosaico_usb_audio_initialized());
    ++calls[CONSOLE];
    return fails("console") ? ESP_FAIL : ESP_OK;
}

esp_err_t esp_timer_create(const esp_timer_create_args_t *config, esp_timer_handle_t *timer) {
    assert(mosaico_usb_audio_initialized() && config->callback && !config->arg);
    assert(config->dispatch_method == ESP_TIMER_TASK && config->skip_unhandled_events);
    ++timer_creates;
    if (fails("timer_create"))
        return ESP_FAIL;
    status_timer = *config;
    *timer = &status_timer;
    return ESP_OK;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period) {
    assert(timer == &status_timer && period == 5000000 && mosaico_usb_audio_initialized());
    ++timer_starts;
    return fails("timer_start") ? ESP_FAIL : ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    assert(timer == &status_timer && fails("timer_start"));
    ++timer_deletes;
    return ESP_OK;
}

void mosaico_usb_audio_event(tinyusb_event_t *event, void *argument) {
    assert(event && !argument);
    ++events;
}

void mosaico_usb_audio_log_stats(void) {
    ++status_logs;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    scenario = argv[1];
    assert(!mosaico_usb_audio_initialized());
    bool core_ok =
        fails("normal") || fails("console") || fails("timer_create") || fails("timer_start");
    esp_err_t expected =
        fails("normal") || fails("timer_create") || fails("timer_start") ? ESP_OK : ESP_FAIL;
    assert(mosaico_usb_console_init() == expected);
    assert(mosaico_usb_audio_initialized() == core_ok);
    if (!core_ok) {
        assert(!timer_creates && !calls[CONSOLE]);
        assert(uninstalls == (unsigned)(fails("cdc_class") || fails("cdc_uninstall")));
    } else {
        assert(installed && !uninstalls && calls[CONSOLE] == 1);
        assert(timer_creates == (unsigned)!fails("console"));
        assert(timer_starts == (unsigned)(!fails("console") && !fails("timer_create")));
        assert(timer_deletes == (unsigned)fails("timer_start"));
        if (fails("normal")) {
            status_timer.callback(status_timer.arg);
            assert(status_logs == 1);
        }
        /* A disconnected/suspended host does not remove initialized hardware. */
        tinyusb_event_t event = {0};
        installed_config.event_cb(&event, installed_config.event_arg);
        event.id = 1;
        installed_config.event_cb(&event, installed_config.event_arg);
        assert(events == 2 && mosaico_usb_audio_initialized());
    }
    unsigned first_calls[STAGE_COUNT];
    memcpy(first_calls, calls, sizeof(calls));
    assert(mosaico_usb_console_init() == expected);
    assert(!memcmp(first_calls, calls, sizeof(calls)) && timer_creates <= 1 && uninstalls <= 1);
    assert(mosaico_usb_audio_initialized() == core_ok);
    printf("PASS Mosaico composite initialization %s\n", scenario);
    return 0;
}
