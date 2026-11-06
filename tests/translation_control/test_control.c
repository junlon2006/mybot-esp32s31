/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the production result mailbox, HTTP worker and teardown ordering. */
#include "../../components/mybot_stack/mybot_sdk/mybot/src/core/mybot_app.c"

static unsigned allocations, delivered, http_starts, http_stops, notifications, shutdowns;
static unsigned tests, finished, kv_deinitialized, service_joins;
static bool fail_queue, disable_during_start, fail_first_service_join, run_control_finalizer;
static bool fail_control_join, fail_service_join, fail_physical_stop, fail_physical_destroy;
static bool fail_usb_stop, fail_usb_destroy, fail_rtc_fini, rtc_pending;
static unsigned physical_stops, physical_destroys, usb_stops, usb_destroys, rtc_finalizations;
static unsigned aosl_releases;
#define runtime s_default_runtime
static mybot_device_service_request_t last_result;
static aosl_mpq_func_t pending_notification;
static uintptr_t notification_argument;

void test_log(const char *format, ...) {
    (void)format;
}
void aosl_msleep(unsigned milliseconds) {
    (void)milliseconds;
}
void *aosl_hal_malloc(size_t size) {
    void *memory = malloc(size);
    if (memory)
        allocations++;
    return memory;
}
void aosl_hal_free(void *memory) {
    if (memory)
        allocations--;
    free(memory);
}
int aosl_mpq_queue(aosl_mpq_t target, aosl_mpq_t source, aosl_ref_t ref, const char *name,
                   aosl_mpq_func_t callback, uintptr_t argc, ...) {
    (void)source;
    (void)ref;
    assert(target == runtime.control_mpq);
    assert(strcmp(name, "handle_service_result") == 0);
    assert(argc == 1); /* Notifications may own the runtime pointer, never a freed request. */
    if (fail_queue)
        return -1;
    va_list arguments;
    va_start(arguments, argc);
    notification_argument = va_arg(arguments, uintptr_t);
    va_end(arguments);
    assert(notification_argument == (uintptr_t)&runtime);
    pending_notification = callback;
    notifications++;
    return 0;
}
int aosl_mpq_destroy_wait(aosl_mpq_t queue) {
    if (queue == runtime.control_mpq && run_control_finalizer) {
        if (fail_control_join)
            return -1;
        control_worker_fini(&runtime);
        return 0;
    }
    assert(queue == runtime.service_mpq);
    service_joins++;
    if (fail_service_join || (fail_first_service_join && service_joins == 1))
        return -1;
    return 0;
}
int aosl_mpq_kill_timer(aosl_timer_t timer) {
    (void)timer;
    return 0;
}
void mybot_key_deinit(mybot_key_t *key) {
    (void)key;
}
int mybot_media_pipeline_stop(mybot_media_pipeline_t *media) {
    assert(!runtime.service_results_enabled);
    assert(media->initialized && (media == &runtime.media || media == runtime.usb_media));
    if (media == &runtime.media) {
        physical_stops++;
        if (fail_physical_stop)
            return -1;
    } else {
        usb_stops++;
        if (fail_usb_stop)
            return -1;
    }
    media->running = media->rtc_connected = false;
    media->cap_mpq = media->pb_mpq = media->send_mpq = AOSL_MPQ_INVALID;
    media->stop_complete = true;
    return 0;
}
int mybot_media_pipeline_destroy(mybot_media_pipeline_t *media) {
    assert(media->initialized && media->stop_complete && !rtc_pending);
    if (media == &runtime.media) {
        physical_destroys++;
        if (fail_physical_destroy)
            return -1;
    } else {
        assert(media == runtime.usb_media);
        usb_destroys++;
        if (fail_usb_destroy)
            return -1;
    }
    media->initialized = false;
    media->cap_ctx = media->pb_ctx = NULL;
    return 0;
}
int mybot_agora_rtc_fini(void) {
    rtc_finalizations++;
    if (fail_rtc_fini)
        return -1;
    rtc_pending = false;
    return 0;
}
void mybot_kv_store_deinit(mybot_kv_store_t *store) {
    if (store->ctx) {
        kv_deinitialized++;
        store->ctx = NULL;
    }
}
void mybot_wifi_deinit(mybot_wifi_t *wifi) {
    (void)wifi;
}
void mybot_presenter_show_screen(mybot_presenter_t *presenter, mybot_lcd_screen_t screen) {
    (void)presenter;
    (void)screen;
}
void mybot_presenter_deinit(mybot_presenter_t *presenter) {
    (void)presenter;
}
void aosl_dtor(void) {
    assert(!runtime.media.initialized && !runtime.usb_media && !runtime.silent_audio);
    assert(runtime.control_mpq == AOSL_MPQ_INVALID && runtime.service_mpq == AOSL_MPQ_INVALID);
    assert(!runtime.lifecycle_initialized && !runtime.kv_store.ctx && !rtc_pending);
    assert(mybot_get_state() == MYBOT_STATE_STOPPED);
    aosl_releases++;
}
void mybot_device_lifecycle_shutdown(mybot_device_lifecycle_t *lifecycle) {
    assert(lifecycle == &runtime.lifecycle);
    assert(!runtime.service_results_enabled);
    shutdowns++;
}
void mybot_device_lifecycle_finish_shutdown(mybot_device_lifecycle_t *lifecycle) {
    assert(lifecycle == &runtime.lifecycle);
    assert(!runtime.completed_results);
    finished++;
}
void mybot_device_lifecycle_service_result(mybot_device_lifecycle_t *lifecycle,
                                           const mybot_device_service_request_t *request) {
    assert(lifecycle == &runtime.lifecycle);
    last_result = *request;
    delivered++;
}
int mybot_device_client_start_conversation(const char *base, const char *id, const char *token,
                                           mybot_translation_mode_t mode, const char *source,
                                           const char *target,
                                           mybot_device_conversation_t *response) {
    (void)base;
    (void)id;
    (void)token;
    assert(mode == MYBOT_TRANSLATION_LISTEN);
    assert(strcmp(source, "en-US") == 0 && strcmp(target, "zh-CN") == 0);
    http_starts++;
    if (disable_during_start)
        runtime.service_results_enabled = 0;
    memset(response, 0, sizeof(*response));
    strcpy(response->conversation_id, "cloud-parent-returned-during-stop");
    return 0;
}
int mybot_device_client_stop_conversation(const char *base, const char *id, const char *token,
                                          const char *parent, const char *reason) {
    (void)base;
    (void)id;
    (void)token;
    (void)parent;
    (void)reason;
    http_stops++;
    return 0;
}
int mybot_device_client_get_binding_status(const char *base, const char *id, const char *auth,
                                           mybot_device_binding_t *response) {
    (void)base;
    (void)id;
    (void)auth;
    (void)response;
    abort();
}

static void reset(void) {
    assert(allocations == 0);
    memset(&runtime, 0, sizeof(runtime));
    memset(&last_result, 0, sizeof(last_result));
    s_lifecycle_gate = s_exit_generation = s_run_exit_generation = 0;
    runtime.running = runtime.aosl_ref_held = true;
    assert(mybot_state_model_begin_start(&runtime.state_model));
    assert(mybot_state_model_begin_services(&runtime.state_model));
    assert(mybot_state_model_services_ready(&runtime.state_model));
    runtime.lifecycle_initialized = true;
    runtime.kv_store.ctx = &runtime.kv_store;
    runtime.service_results_enabled = 1;
    runtime.control_mpq = 1;
    runtime.service_mpq = 2;
    runtime.control_timer = AOSL_MPQ_TIMER_INVALID;
    delivered = http_starts = http_stops = notifications = shutdowns = finished = 0;
    kv_deinitialized = service_joins = 0;
    fail_queue = disable_during_start = false;
    fail_first_service_join = run_control_finalizer = false;
    fail_control_join = fail_service_join = fail_physical_stop = fail_physical_destroy = false;
    fail_usb_stop = fail_usb_destroy = fail_rtc_fini = rtc_pending = false;
    physical_stops = physical_destroys = usb_stops = usb_destroys = rtc_finalizations = 0;
    aosl_releases = 0;
    pending_notification = NULL;
    notification_argument = 0;
}
static mybot_device_service_request_t *request(mybot_device_service_request_type_t type) {
    mybot_device_service_request_t *result = aosl_hal_malloc(sizeof(*result));
    assert(result);
    memset(result, 0, sizeof(*result));
    result->type = type;
    if (type == MYBOT_DEVICE_SERVICE_START) {
        strcpy(result->payload.start.source_language, "en-US");
        strcpy(result->payload.start.target_language, "zh-CN");
    }
    return result;
}
static void execute(mybot_device_service_request_t *work) {
    uintptr_t argv[] = {(uintptr_t)&runtime, (uintptr_t)work};
    execute_service_request(NULL, NULL, 2, argv);
}
static void notify(void) {
    assert(pending_notification);
    aosl_mpq_func_t callback = pending_notification;
    pending_notification = NULL;
    callback(NULL, NULL, 1, &notification_argument);
}
static void test_normal_mailbox(void) {
    reset();
    execute(request(MYBOT_DEVICE_SERVICE_START));
    assert(http_starts == 1 && delivered == 0 && allocations == 1 && notifications == 1);
    assert(runtime.completed_results);
    notify();
    assert(delivered == 1 && allocations == 0 && !runtime.completed_results);
    assert(strcmp(last_result.payload.start.response.conversation_id,
                  "cloud-parent-returned-during-stop") == 0);
    apply_completed_services(&runtime);
    assert(delivered == 1);
    tests++;
}
static void test_start_not_executed(void) {
    reset();
    runtime.service_results_enabled = 0;
    execute(request(MYBOT_DEVICE_SERVICE_START));
    assert(http_starts == 0 && notifications == 0 && runtime.completed_results);
    apply_completed_services(&runtime);
    assert(delivered == 1 && last_result.result < 0);
    assert(!last_result.payload.start.response.conversation_id[0]);
    assert(!last_result.payload.start.response.start_unknown);
    assert(strcmp(last_result.payload.start.response.error_code, "START_CANCELLED") == 0);
    assert(allocations == 0);
    tests++;
}
static void test_disabled_after_start(void) {
    reset();
    disable_during_start = true;
    execute(request(MYBOT_DEVICE_SERVICE_START));
    assert(http_starts == 1 && notifications == 0 && runtime.completed_results);
    cleanup_services(&runtime);
    assert(shutdowns == 1 && finished == 1 && delivered == 1 && allocations == 0);
    assert(strcmp(last_result.payload.start.response.conversation_id,
                  "cloud-parent-returned-during-stop") == 0);
    tests++;
}
static void test_notification_failure(void) {
    reset();
    fail_queue = true;
    execute(request(MYBOT_DEVICE_SERVICE_START));
    assert(http_starts == 1 && delivered == 0 && runtime.completed_results);
    apply_completed_services(&runtime);
    assert(delivered == 1 && allocations == 0);
    assert(last_result.payload.start.response.conversation_id[0]);
    tests++;
}
static void test_late_notification(void) {
    reset();
    execute(request(MYBOT_DEVICE_SERVICE_START));
    assert(pending_notification && allocations == 1);
    cleanup_services(&runtime);
    assert(delivered == 1 && allocations == 0 && !runtime.lifecycle_initialized);
    notify(); /* Cleanup already freed the request; notification owns only runtime. */
    assert(delivered == 1 && allocations == 0 && !runtime.completed_results);
    tests++;
}
static void test_shutdown_stop_and_poll(void) {
    reset();
    runtime.service_results_enabled = 0;
    execute(request(MYBOT_DEVICE_SERVICE_STOP));
    assert(http_stops == 1 && runtime.completed_results && notifications == 0);
    apply_completed_services(&runtime);
    assert(delivered == 1 && allocations == 0);
    execute(request(MYBOT_DEVICE_SERVICE_DEVICE_POLL));
    assert(delivered == 1 && allocations == 0 && !runtime.completed_results);
    tests++;
}
static void test_service_join_retry(void) {
    reset();
    disable_during_start = true;
    execute(request(MYBOT_DEVICE_SERVICE_START));
    assert(runtime.completed_results && allocations == 1 && notifications == 0);
    fail_first_service_join = true;
    run_control_finalizer = true;
    assert(destroy_control_queue(&runtime));
    assert(service_joins == 2 && shutdowns == 1);
    assert(delivered == 1 && finished == 1 && kv_deinitialized == 1);
    assert(strcmp(last_result.payload.start.response.conversation_id,
                  "cloud-parent-returned-during-stop") == 0);
    assert(runtime.control_mpq == AOSL_MPQ_INVALID);
    assert(runtime.service_mpq == AOSL_MPQ_INVALID);
    assert(!runtime.lifecycle_initialized && !runtime.completed_results && allocations == 0);
    tests++;
}

static void retain_physical_media(void) {
    runtime.media.initialized = true;
    runtime.media.running = true;
    runtime.media.cap_ctx = runtime.media.pb_ctx = &runtime.media;
    runtime.media.cap_mpq = 11;
    runtime.media.pb_mpq = 12;
    runtime.media.send_mpq = 13;
    runtime.silent_audio = aosl_hal_malloc(16);
    assert(runtime.silent_audio);
    run_control_finalizer = true;
}

static void assert_stop_retained(void) {
    assert(!aosl_releases && runtime.aosl_ref_held && !mybot_is_running());
    assert(mybot_get_state() == MYBOT_STATE_STOPPING);
    assert(runtime.silent_audio && runtime.media.initialized);
}

static void test_stop_before_media_initialization(void) {
    reset();
    runtime.lifecycle_initialized = false;
    runtime.kv_store.ctx = NULL;
    runtime.service_mpq = AOSL_MPQ_INVALID;
    run_control_finalizer = true;
    mybot_stop();
    assert(aosl_releases == 1 && !runtime.aosl_ref_held && !allocations);
    assert(!physical_stops && !physical_destroys && !usb_stops && !usb_destroys);
    tests++;
}

static void test_physical_stop_and_destroy_retry(void) {
    for (unsigned failure = 0; failure < 2; ++failure) {
        reset();
        retain_physical_media();
        fail_physical_stop = failure == 0;
        fail_physical_destroy = failure == 1;
        int16_t *retained = runtime.silent_audio;
        mybot_stop();
        assert_stop_retained();
        assert(runtime.silent_audio == retained && !runtime.usb_media);
        assert(failure ? runtime.media.stop_complete : runtime.media.cap_mpq == 11);
        assert(runtime.control_mpq == AOSL_MPQ_INVALID);
        fail_physical_stop = fail_physical_destroy = false;
        mybot_stop();
        assert(aosl_releases == 1 && !runtime.aosl_ref_held && !allocations);
        assert(!runtime.media.initialized && !runtime.silent_audio);
        tests++;
    }
}

static void test_standalone_rtc_cleanup_retry(void) {
    reset();
    retain_physical_media();
    fail_rtc_fini = rtc_pending = true;
    mybot_stop();
    assert_stop_retained();
    assert(!physical_destroys && rtc_finalizations >= 2 && rtc_pending);
    mybot_stop();
    assert_stop_retained(); /* A terminal vendor failure must remain blocked. */
    fail_rtc_fini = false;  /* A retryable forwarding-queue failure can instead clear. */
    mybot_stop();
    assert(aosl_releases == 1 && !runtime.aosl_ref_held && !rtc_pending && !allocations);
    tests++;
}

static void test_usb_and_physical_shutdown_retry(void) {
    for (unsigned failure = 0; failure < 3; ++failure) {
        reset();
        retain_physical_media();
        runtime.usb_media = aosl_hal_malloc(sizeof(*runtime.usb_media));
        assert(runtime.usb_media);
        memset(runtime.usb_media, 0, sizeof(*runtime.usb_media));
        runtime.usb_media->initialized = true;
        runtime.usb_media->running = true;
        mybot_media_pipeline_t *retained = runtime.usb_media;
        fail_physical_stop = failure == 0;
        fail_usb_stop = failure == 1;
        fail_usb_destroy = failure == 2;
        mybot_stop();
        assert(!aosl_releases && runtime.aosl_ref_held && runtime.usb_media == retained);
        assert(mybot_get_state() == MYBOT_STATE_STOPPING && physical_stops && usb_stops);
        assert(runtime.silent_audio);
        if (failure < 2)
            assert(!physical_destroys && !usb_destroys);
        fail_physical_stop = fail_usb_stop = fail_usb_destroy = false;
        mybot_stop();
        assert(aosl_releases == 1 && !runtime.aosl_ref_held && !runtime.usb_media);
        assert(!allocations);
        tests++;
    }
}

static void test_pending_service_and_control_join(void) {
    reset();
    retain_physical_media();
    fail_service_join = true;
    mybot_stop();
    assert(runtime.aosl_ref_held && !aosl_releases && runtime.lifecycle_initialized);
    assert(mybot_get_state() == MYBOT_STATE_STOPPING && runtime.kv_store.ctx);
    fail_service_join = false;
    mybot_stop();
    assert(aosl_releases == 1 && !runtime.aosl_ref_held && !allocations);

    reset();
    retain_physical_media();
    fail_control_join = true;
    mybot_stop();
    assert(runtime.aosl_ref_held && !aosl_releases && runtime.media.initialized);
    assert(runtime.control_mpq != AOSL_MPQ_INVALID && !physical_stops);
    fail_control_join = false;
    mybot_stop();
    assert(aosl_releases == 1 && !runtime.aosl_ref_held && !allocations);
    tests++;
}

static void test_pending_service_after_lifecycle_init_failure(void) {
    reset();
    runtime.lifecycle_initialized = false;
    run_control_finalizer = true;
    fail_service_join = true;
    mybot_stop();
    assert(runtime.aosl_ref_held && !aosl_releases && runtime.kv_store.ctx);
    assert(mybot_get_state() == MYBOT_STATE_STOPPING);
    fail_service_join = false;
    mybot_stop();
    assert(aosl_releases == 1 && !runtime.aosl_ref_held && !runtime.kv_store.ctx);
    assert(kv_deinitialized == 1 && !allocations);
    tests++;
}

int main(void) {
    test_normal_mailbox();
    test_start_not_executed();
    test_disabled_after_start();
    test_notification_failure();
    test_late_notification();
    test_shutdown_stop_and_poll();
    test_service_join_retry();
    test_stop_before_media_initialization();
    test_physical_stop_and_destroy_retry();
    test_standalone_rtc_cleanup_retry();
    test_usb_and_physical_shutdown_retry();
    test_pending_service_and_control_join();
    test_pending_service_after_lifecycle_init_failure();
    printf("translation control: %u source-level result-mailbox/shutdown scenarios passed\n",
           tests);
    return 0;
}
