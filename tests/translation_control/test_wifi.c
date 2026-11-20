/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../components/mybot_stack/mybot_sdk/mybot/src/core/mybot_app.c"
#include "../../components/mybot_stack/mybot_sdk/mybot/src/core/mybot_device_lifecycle.c"

#define runtime s_default_runtime

static struct {
    aosl_mpq_func_t run;
    uintptr_t argc, args[4];
} events[8];
static unsigned queued, consumed;
static bool reconnect_during_init;
static mybot_lcd_screen_t screen;
static mybot_wifi_event_handler_t notify_wifi;
static void *wifi_context;

int aosl_mpq_queue(aosl_mpq_t target, aosl_mpq_t source, aosl_ref_t ref, const char *name,
                   aosl_mpq_func_t callback, uintptr_t argc, ...) {
    assert(target == runtime.control_mpq && strcmp(name, "handle_wifi_event") == 0);
    assert(queued < sizeof(events) / sizeof(events[0]) && argc <= 4);
    events[queued].run = callback;
    events[queued].argc = argc;
    va_list args;
    va_start(args, argc);
    for (uintptr_t i = 0; i < argc; ++i)
        events[queued].args[i] = va_arg(args, uintptr_t);
    va_end(args);
    ++queued;
    return 0;
}

static void dispatch(void) {
    assert(consumed < queued);
    unsigned i = consumed++;
    events[i].run(NULL, NULL, events[i].argc, events[i].args);
}

/* start_services opens storage before initializing the lifecycle. */
int mybot_kv_store_init(mybot_kv_store_t *store) {
    notify_wifi(MYBOT_WIFI_EVENT_STA_DISCONNECTED, wifi_context);
    return 0;
}

int mybot_kv_store_get(mybot_kv_store_t *store, const char *key, void *value, size_t capacity,
                       size_t *length) {
    if (strcmp(key, DEVICE_AUTH_KEY))
        return MYBOT_ERR_NOT_FOUND;
    const device_auth_record_t auth = {.version = RECORD_VERSION,
                                       .server_base = "https://service.invalid",
                                       .device_id = "fixture",
                                       .device_token = "synthetic"};
    assert(capacity >= sizeof(auth));
    memcpy(value, &auth, sizeof(auth));
    *length = sizeof(auth);
    return 0;
}

/* Starting the control timer is the first queued-control operation after init. */
aosl_timer_t aosl_mpq_set_timer(uintptr_t interval, aosl_timer_func_t callback, void *destructor,
                                uintptr_t argc, ...) {
    assert(callback == control_tick_timer && runtime.lifecycle_initialized);
    if (reconnect_during_init)
        notify_wifi(MYBOT_WIFI_EVENT_STA_CONNECTED, wifi_context);
    return 1;
}

static int render(void *ctx, const mybot_lcd_content_t *content) {
    screen = content->screen;
    return 0;
}
static int open_lcd(void **ctx) {
    return 0;
}
static const mybot_lcd_ops_t lcd_ops = {.init = open_lcd, .render = render};

static int open_wifi(void **ctx, const char *id, mybot_wifi_event_handler_t handler, void *arg) {
    notify_wifi = handler;
    wifi_context = arg;
    handler(MYBOT_WIFI_EVENT_STA_CONNECTED, arg);
    return 0;
}
static const mybot_wifi_ops_t wifi_ops = {.init = open_wifi};

static void initialize(bool reconnect) {
    memset(&runtime, 0, sizeof(runtime));
    queued = consumed = 0;
    reconnect_during_init = reconnect;
    runtime.control_mpq = 1;
    runtime.lifecycle.network_generation = 42;
    strcpy(runtime.config.device_id, "fixture");
    strcpy(runtime.config.server_base, "https://service.invalid");
    strcpy(runtime.config.source_language, "en-US");
    strcpy(runtime.config.target_language, "zh-CN");
    uintptr_t args[] = {(uintptr_t)&runtime};
    handle_control_start(NULL, NULL, 1, args);
    assert(runtime.control_start_result == 0);
    dispatch();
    assert(runtime.lifecycle_initialized);
}

static void test_reconnect_straddles_init(void) {
    initialize(true);
    dispatch(); /* Queued disconnect predates the reconnect, which is already published. */
    dispatch();
    assert(screen == MYBOT_LCD_SCREEN_READY);
    mybot_device_lifecycle_tick(&runtime.lifecycle);
    mybot_device_lifecycle_request_start(&runtime.lifecycle);
    assert(runtime.lifecycle.conversation_requested);
    assert(runtime.lifecycle.network_generation == 1);

    runtime.lifecycle.conversation_requested = false;
    notify_wifi(MYBOT_WIFI_EVENT_STA_DISCONNECTED, wifi_context);
    mybot_device_lifecycle_request_start(&runtime.lifecycle);
    assert(!runtime.lifecycle.conversation_requested && !runtime.lifecycle.network_available);
    dispatch();
    assert(screen == MYBOT_LCD_SCREEN_WIFI_DISCONNECTED);
    mybot_captions_destroy(runtime.captions);
}

static void test_latest_disconnect_survives_init(void) {
    initialize(false);
    assert(!runtime.lifecycle.network_available && runtime.lifecycle.network_loss_pending);
    dispatch();
    assert(screen == MYBOT_LCD_SCREEN_WIFI_DISCONNECTED);
    notify_wifi(MYBOT_WIFI_EVENT_STA_CONNECTED, wifi_context);
    dispatch();
    assert(screen == MYBOT_LCD_SCREEN_READY);
    mybot_device_lifecycle_tick(&runtime.lifecycle);
    mybot_device_lifecycle_request_start(&runtime.lifecycle);
    assert(runtime.lifecycle.conversation_requested);
    assert(runtime.lifecycle.network_generation == 1);
    mybot_captions_destroy(runtime.captions);
}

int main(void) {
    test_reconnect_straddles_init();
    test_latest_disconnect_survives_init();
    puts("Wi-Fi initialization: queued reconnect admission and retained disconnect passed");
    return 0;
}

/* Device, worker and network boundaries touched by service initialization. */
void *aosl_hal_malloc(size_t size) {
    return malloc(size);
}
void aosl_hal_free(void *memory) {
    free(memory);
}
void test_log(const char *format, ...) {
    (void)format;
}
void aosl_msleep(unsigned ms) {
    abort();
}
aosl_mpq_t aosl_mpq_create_flags(int flags, int priority, int stack, int maximum, const char *name,
                                 void *init, void *fini, void *arg) {
    return 2;
}
int aosl_mpq_destroy_wait(aosl_mpq_t queue) {
    return 0;
}
int aosl_mpq_kill_timer(aosl_timer_t timer) {
    return 0;
}
int mybot_key_init(mybot_key_t *key, mybot_key_event_handler_t handler, void *context) {
    return 0;
}
void mybot_key_deinit(mybot_key_t *key) {
}
void mybot_kv_store_deinit(mybot_kv_store_t *store) {
}
int mybot_kv_store_set(mybot_kv_store_t *store, const char *key, const void *value, size_t size) {
    abort();
}
int mybot_kv_store_erase(mybot_kv_store_t *store, const char *key) {
    abort();
}
void mybot_media_pipeline_init(mybot_media_pipeline_t *pipeline) {
}
int mybot_media_pipeline_start(mybot_media_pipeline_t *pipeline,
                               const mybot_media_pipeline_callbacks_t *callbacks) {
    return 0;
}
int mybot_media_pipeline_get_volume(mybot_media_pipeline_t *pipeline, int *volume) {
    *volume = 50;
    return 0;
}
void mybot_media_pipeline_stop_prompt(mybot_media_pipeline_t *pipeline) {
}
bool mybot_media_pipeline_is_paused(const mybot_media_pipeline_t *pipeline) {
    return false;
}
int mybot_agora_rtc_fini(void) {
    return 0;
}
int mybot_agora_rtc_init(const char *id, const mybot_agora_rtc_callbacks_t *callbacks) {
    abort();
}
int mybot_agora_rtc_join(const mybot_device_translation_route_t *routes, size_t count) {
    abort();
}
int mybot_device_client_create_pair_code(const char *base, const char *id, const char *fw,
                                         const char *model, mybot_device_pair_code_t *response) {
    abort();
}
int mybot_device_client_get_binding_status(const char *base, const char *id, const char *auth,
                                           mybot_device_binding_t *response) {
    abort();
}
int mybot_device_client_start_conversation(const char *base, const char *id, const char *auth,
                                           mybot_translation_mode_t mode, const char *source,
                                           const char *target,
                                           mybot_device_conversation_t *response) {
    abort();
}
int mybot_device_client_stop_conversation(const char *base, const char *id, const char *auth,
                                          const char *parent, const char *reason) {
    abort();
}
int mybot_media_pipeline_destroy(mybot_media_pipeline_t *pipeline) {
    abort();
}
int mybot_media_pipeline_end_session(mybot_media_pipeline_t *pipeline) {
    abort();
}
int mybot_media_pipeline_flush_session(mybot_media_pipeline_t *pipeline) {
    abort();
}
int mybot_media_pipeline_play_prompt(mybot_media_pipeline_t *pipeline, mybot_prompt_type_t type,
                                     const void *args) {
    abort();
}
void mybot_media_pipeline_push_remote_audio(mybot_media_pipeline_t *pipeline, const void *data,
                                            size_t len) {
    abort();
}
int mybot_media_pipeline_quiesce(mybot_media_pipeline_t *pipeline) {
    abort();
}
void mybot_media_pipeline_set_rtc_connected(mybot_media_pipeline_t *pipeline, bool connected) {
    abort();
}
int mybot_media_pipeline_stop(mybot_media_pipeline_t *pipeline) {
    abort();
}
const mybot_platform_descriptor_t *mybot_platform_registry_get(void) {
    static const mybot_platform_descriptor_t platform = {.lcd = &lcd_ops, .wifi = &wifi_ops};
    return &platform;
}
int mybot_agora_rtc_send_audio(mybot_translation_direction_t direction, const void *data,
                               size_t len) {
    abort();
}
int mybot_media_pipeline_adjust_volume(mybot_media_pipeline_t *pipeline, int delta) {
    abort();
}
int mybot_media_pipeline_start_usb(mybot_media_pipeline_t *pipeline,
                                   const mybot_media_pipeline_callbacks_t *callbacks) {
    abort();
}
