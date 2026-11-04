/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../components/mybot_stack/mybot_sdk/mybot/src/core/mybot_app.c"

#define runtime s_default_runtime

typedef struct {
    aosl_mpq_func_t function;
    uintptr_t arguments[2];
} notification_t;
static notification_t notifications[32];
static unsigned notification_count, language_sets, volume_sets, pause_sets, renders, tests;
static unsigned conversation_starts, session_ends, rtc_finalizations;
static bool fail_queue, fail_volume, fail_pause, fail_end_session, paused;
static int hardware_volume, rtc_fini_result;
static mybot_lcd_content_t displayed;

void *aosl_hal_malloc(size_t size) {
    return malloc(size);
}
void aosl_hal_free(void *memory) {
    free(memory);
}

void test_log(const char *format, ...) {
    (void)format;
}
void aosl_msleep(unsigned milliseconds) {
    (void)milliseconds;
    assert(!"a noncontended UI command must never wait");
}
int aosl_mpq_queue(aosl_mpq_t target, aosl_mpq_t source, aosl_ref_t reference, const char *name,
                   aosl_mpq_func_t callback, uintptr_t argc, ...) {
    assert(target == runtime.control_mpq && source == AOSL_MPQ_INVALID);
    assert(reference == AOSL_REF_INVALID && strcmp(name, "handle_ui_command") == 0);
    assert(argc == 2);
    if (fail_queue)
        return -1;
    assert(notification_count < sizeof(notifications) / sizeof(notifications[0]));
    notification_t *notification = &notifications[notification_count++];
    notification->function = callback;
    va_list arguments;
    va_start(arguments, argc);
    notification->arguments[0] = va_arg(arguments, uintptr_t);
    notification->arguments[1] = va_arg(arguments, uintptr_t);
    va_end(arguments);
    return 0;
}
int mybot_device_lifecycle_set_languages(mybot_device_lifecycle_t *lifecycle, const char *source,
                                         const char *target) {
    assert(lifecycle == &runtime.lifecycle);
    language_sets++;
    if (runtime_get_state(&runtime) != MYBOT_STATE_READY)
        return -1;
    snprintf(lifecycle->source_language, sizeof(lifecycle->source_language), "%s", source);
    snprintf(lifecycle->target_language, sizeof(lifecycle->target_language), "%s", target);
    return 0;
}
int mybot_media_pipeline_get_volume(mybot_media_pipeline_t *pipeline, int *volume) {
    assert(pipeline == &runtime.media);
    *volume = hardware_volume;
    return 0;
}
int mybot_media_pipeline_set_volume(mybot_media_pipeline_t *pipeline, int volume) {
    assert(pipeline == &runtime.media && volume >= 0 && volume <= 100);
    volume_sets++;
    if (fail_volume)
        return -1;
    hardware_volume = volume;
    return 0;
}
int mybot_media_pipeline_set_paused(mybot_media_pipeline_t *pipeline, bool value) {
    assert(pipeline == &runtime.media);
    pause_sets++;
    if (fail_pause)
        return -1;
    paused = value;
    return 0;
}
bool mybot_media_pipeline_is_paused(const mybot_media_pipeline_t *pipeline) {
    assert(pipeline == &runtime.media);
    return paused;
}
int mybot_media_pipeline_end_session(mybot_media_pipeline_t *pipeline) {
    assert(pipeline == &runtime.media);
    session_ends++;
    if (fail_end_session)
        return -1;
    paused = false;
    return 0;
}
int mybot_agora_rtc_fini(void) {
    rtc_finalizations++;
    return rtc_fini_result;
}
void mybot_device_lifecycle_request_start(mybot_device_lifecycle_t *lifecycle) {
    assert(lifecycle == &runtime.lifecycle);
    conversation_starts++;
}
int mybot_lcd_show_content(mybot_lcd_t *lcd, const mybot_lcd_content_t *content) {
    assert(lcd == &runtime.presenter.lcd);
    displayed = *content;
    renders++;
    return 0;
}

static void reset(void) {
    memset(&runtime, 0, sizeof(runtime));
    memset(notifications, 0, sizeof(notifications));
    memset(&displayed, 0, sizeof(displayed));
    notification_count = language_sets = volume_sets = pause_sets = renders = 0;
    conversation_starts = session_ends = rtc_finalizations = 0;
    fail_queue = fail_volume = fail_pause = fail_end_session = paused = false;
    hardware_volume = 40;
    rtc_fini_result = 0;
    s_lifecycle_gate = s_exit_generation = s_run_exit_generation = 0;
    runtime.running = runtime.ui_accepting_commands = 1;
    runtime.ui_generation = 17;
    runtime.control_mpq = 123;
    runtime.presenter.active = true;
    assert(mybot_state_model_begin_start(&runtime.state_model));
    assert(mybot_state_model_begin_services(&runtime.state_model));
    assert(mybot_state_model_services_ready(&runtime.state_model));
    assert(mybot_state_model_set_device_state(&runtime.state_model, MYBOT_DEVICE_STATE_RUNTIME));
    strcpy(runtime.lifecycle.source_language, "zh-CN");
    strcpy(runtime.lifecycle.target_language, "en-US");
}
static void dispatch(unsigned index) {
    assert(index < notification_count);
    notification_t notification = notifications[index];
    notification.function(NULL, NULL, 2, notification.arguments);
}
static mybot_ui_command_t volume(int value) {
    mybot_ui_command_t result = {.type = MYBOT_UI_SET_VOLUME, .value = value};
    return result;
}
static mybot_ui_command_t languages(const char *source, const char *target) {
    mybot_ui_command_t result = {.type = MYBOT_UI_SET_LANGUAGES};
    snprintf(result.source_language, sizeof(result.source_language), "%s", source);
    snprintf(result.target_language, sizeof(result.target_language), "%s", target);
    return result;
}
static void active(void) {
    assert(mybot_state_model_set_device_state(&runtime.state_model,
                                              MYBOT_DEVICE_STATE_IN_CONVERSATION));
    runtime.media.rtc_connected = 1;
}
static void test_validation_and_language_table(void) {
    reset();
    size_t count = 0;
    const mybot_ui_language_t *list = mybot_ui_languages(&count);
    assert(count == 15 && list == mybot_ui_languages(NULL));
    for (size_t i = 0; i < count; ++i) {
        assert(list[i].code[0] && list[i].name_zh[0] && list[i].name_en[0]);
        for (size_t j = i + 1; j < count; ++j)
            assert(strcmp(list[i].code, list[j].code) != 0);
    }
    assert(mybot_ui_submit(NULL) == -1);
    mybot_ui_command_t command = volume(-1);
    assert(mybot_ui_submit(&command) == -1);
    command.value = 101;
    assert(mybot_ui_submit(&command) == -1);
    command.type = MYBOT_UI_SET_PAUSED;
    command.value = 2;
    assert(mybot_ui_submit(&command) == -1);
    command.type = (mybot_ui_command_type_t)99;
    assert(mybot_ui_submit(&command) == -1);
    command = languages("zh-CN", "zh-CN");
    assert(mybot_ui_submit(&command) == -1);
    command = languages("zz-ZZ", "en-US");
    assert(mybot_ui_submit(&command) == -1);
    memset(command.source_language, 'a', sizeof(command.source_language));
    assert(mybot_ui_submit(&command) == -1);
    assert(notification_count == 0 && runtime.ui_count == 0);
    tests++;
}
static void test_copy_async_and_actual_volume(void) {
    reset();
    mybot_ui_command_t command = volume(65);
    assert(mybot_ui_submit(&command) == 0);
    command.value = 9;
    assert(hardware_volume == 40 && volume_sets == 0 && renders == 0);
    dispatch(0);
    assert(hardware_volume == 65 && displayed.volume == 65 && volume_sets == 1);
    assert(runtime.ui_count == 0);
    command.value = 90;
    fail_volume = true;
    assert(mybot_ui_submit(&command) == 0);
    dispatch(1);
    assert(displayed.volume == 65 && hardware_volume == 65 && volume_sets == 2);
    tests++;
}
static void test_language_idle_and_busy(void) {
    reset();
    mybot_ui_command_t command = languages("ja-JP", "ko-KR");
    assert(mybot_ui_submit(&command) == 0);
    assert(strcmp(runtime.lifecycle.source_language, "zh-CN") == 0);
    dispatch(0);
    assert(strcmp(displayed.source_language, "ja-JP") == 0);
    assert(strcmp(displayed.target_language, "ko-KR") == 0);
    command = languages("en-US", "zh-CN");
    assert(mybot_ui_submit(&command) == 0);
    active(); /* Valid at submission, busy before application. */
    dispatch(1);
    assert(strcmp(displayed.source_language, "ja-JP") == 0 && language_sets == 2);
    tests++;
}
static void test_bounded_queue_and_enqueue_failure(void) {
    reset();
    mybot_ui_command_t command = volume(1);
    fail_queue = true;
    assert(mybot_ui_submit(&command) == -1 && runtime.ui_count == 0);
    assert(runtime.ui_gate == 0 && s_lifecycle_gate == 0 && runtime.running);
    fail_queue = false;
    for (int i = 0; i < UI_COMMAND_CAPACITY; ++i) {
        command.value = i;
        assert(mybot_ui_submit(&command) == 0);
    }
    assert(mybot_ui_submit(&command) == -1 && notification_count == UI_COMMAND_CAPACITY);
    for (unsigned i = 0; i < UI_COMMAND_CAPACITY; ++i) {
        dispatch(i);
        assert(hardware_volume == (int)i);
    }
    assert(runtime.ui_count == 0 && runtime.ui_head == 0);
    assert(mybot_ui_submit(&command) == 0);
    dispatch(UI_COMMAND_CAPACITY);
    tests++;
}
static void test_try_lock_and_shutdown(void) {
    reset();
    mybot_ui_command_t command = volume(25);
    s_lifecycle_gate = 1;
    assert(mybot_ui_submit(&command) == -1);
    s_lifecycle_gate = 0;
    runtime.ui_gate = 1;
    assert(mybot_ui_submit(&command) == -1 && s_lifecycle_gate == 0);
    runtime.ui_gate = 0;
    runtime.ui_accepting_commands = 0;
    assert(mybot_ui_submit(&command) == -1);
    runtime.ui_accepting_commands = 1;
    assert(mybot_ui_submit(&command) == 0);
    runtime_publish_exit(&runtime);
    assert(mybot_ui_submit(&command) == -1);
    dispatch(0);
    assert(volume_sets == 0 && runtime.ui_count == 0);
    reset();
    assert(mybot_ui_submit(&command) == 0);
    close_ui_commands(&runtime);
    dispatch(0); /* Notification has no separately owned payload to leak. */
    assert(volume_sets == 0 && runtime.ui_count == 0);
    assert(mybot_ui_submit(&command) == -1);
    tests++;
}
static void test_old_generation_does_not_consume_new_command(void) {
    reset();
    mybot_ui_command_t command = volume(20);
    assert(mybot_ui_submit(&command) == 0);
    close_ui_commands(&runtime);
    runtime.ui_generation++;
    runtime.ui_accepting_commands = 1;
    command.value = 70;
    assert(mybot_ui_submit(&command) == 0);
    dispatch(0);
    assert(volume_sets == 0 && runtime.ui_count == 1);
    dispatch(1);
    assert(volume_sets == 1 && displayed.volume == 70 && runtime.ui_count == 0);
    tests++;
}
static void test_pause_only_active_and_actual_state(void) {
    reset();
    mybot_ui_command_t command = {.type = MYBOT_UI_SET_PAUSED, .value = 1};
    assert(mybot_ui_submit(&command) == 0);
    dispatch(0);
    assert(pause_sets == 0 && !displayed.paused);
    active();
    assert(mybot_ui_submit(&command) == 0);
    dispatch(1);
    assert(pause_sets == 1 && displayed.paused && paused);
    command.value = 0;
    fail_pause = true;
    assert(mybot_ui_submit(&command) == 0);
    dispatch(2);
    assert(pause_sets == 2 && displayed.paused && paused);
    fail_pause = false;
    assert(mybot_ui_submit(&command) == 0);
    dispatch(3);
    assert(pause_sets == 3 && !displayed.paused && !paused);
    command.value = 1;
    assert(mybot_ui_submit(&command) == 0);
    runtime.lifecycle.stop_request = 1;
    dispatch(4);
    assert(pause_sets == 3 && !displayed.paused);
    runtime.lifecycle.stop_request = 0;
    runtime.media_cancel_requested = 1;
    assert(mybot_ui_submit(&command) == 0);
    dispatch(5);
    assert(pause_sets == 3);
    runtime.media_cancel_requested = 0;
    runtime.media.rtc_connected = 0;
    assert(mybot_ui_submit(&command) == 0);
    dispatch(6);
    assert(pause_sets == 3 && !displayed.paused);
    tests++;
}
static void test_media_recovery_before_cloud_start(void) {
    reset();
    active();
    paused = true;
    fail_end_session = true;
    dev_on_conversation_stop(&runtime);
    assert(session_ends == 1 && rtc_finalizations == 1);
    assert(runtime.media_recovery_required && paused);
    assert(mybot_state_model_set_device_state(&runtime.state_model, MYBOT_DEVICE_STATE_RUNTIME));
    control_start_conversation(&runtime);
    assert(session_ends == 2 && conversation_starts == 0 && paused);
    assert(strcmp(runtime.lifecycle.error_code, "MEDIA_NOT_READY") == 0);
    fail_end_session = false;
    control_start_conversation(&runtime);
    assert(session_ends == 3 && conversation_starts == 1 && !paused);
    assert(!runtime.media_recovery_required && !runtime.lifecycle.error_code[0]);
    assert(!runtime.media_cancel_requested);

    reset();
    active();
    fail_end_session = true; /* Flush failure also blocks a start without a paused flag. */
    dev_on_conversation_stop(&runtime);
    assert(runtime.media_recovery_required && !paused);
    assert(mybot_state_model_set_device_state(&runtime.state_model, MYBOT_DEVICE_STATE_RUNTIME));
    control_start_conversation(&runtime);
    assert(conversation_starts == 0 && session_ends == 2);
    fail_end_session = false;
    control_start_conversation(&runtime);
    assert(conversation_starts == 1 && session_ends == 3);
    tests++;
}

static void test_each_session_releases_rtc_before_next_start(void) {
    reset();
    active();
    strcpy(runtime.rtc_channel, "first-project-channel");
    strcpy(runtime.rtc_agent_uid, "first-project-agent");
    dev_on_conversation_stop(&runtime);
    assert(rtc_finalizations == 1 && session_ends == 1);
    assert(runtime.media_cancel_requested && !runtime.rtc_channel[0] && !runtime.rtc_agent_uid[0]);
    assert(mybot_state_model_set_device_state(&runtime.state_model, MYBOT_DEVICE_STATE_RUNTIME));
    control_start_conversation(&runtime);
    assert(rtc_finalizations == 2 && conversation_starts == 1);
    assert(!runtime.media_cancel_requested && !runtime.lifecycle.error_code[0]);

    active();
    dev_on_conversation_stop(&runtime);
    assert(rtc_finalizations == 3 && session_ends == 2);
    assert(mybot_state_model_set_device_state(&runtime.state_model, MYBOT_DEVICE_STATE_RUNTIME));
    control_start_conversation(&runtime);
    assert(rtc_finalizations == 4 && conversation_starts == 2);
    tests++;
}

static void test_rtc_cleanup_failure_blocks_cloud_start(void) {
    reset();
    active();
    rtc_fini_result = -1;
    dev_on_conversation_stop(&runtime);
    assert(rtc_finalizations == 1 && session_ends == 1);
    assert(mybot_state_model_set_device_state(&runtime.state_model, MYBOT_DEVICE_STATE_RUNTIME));
    control_start_conversation(&runtime);
    assert(rtc_finalizations == 2 && conversation_starts == 0);
    assert(runtime.media_cancel_requested);
    assert(strcmp(runtime.lifecycle.error_code, "RTC_NOT_READY") == 0);
    assert(strcmp(displayed.error_code, "RTC_NOT_READY") == 0);
    control_start_conversation(&runtime);
    assert(rtc_finalizations == 3 && conversation_starts == 0);
    assert(strcmp(runtime.lifecycle.error_code, "RTC_NOT_READY") == 0);

    /* A new app runtime must still honor a terminal failure held by the RTC wrapper. */
    reset();
    rtc_fini_result = -1;
    control_start_conversation(&runtime);
    assert(rtc_finalizations == 1 && conversation_starts == 0);
    assert(strcmp(runtime.lifecycle.error_code, "RTC_NOT_READY") == 0);
    assert(strcmp(displayed.error_code, "RTC_NOT_READY") == 0);
    tests++;
}

static void test_rtc_cleanup_failure_preserves_real_error(void) {
    static const struct {
        const char *before;
        const char *after;
    } errors[] = {
        {"", "RTC_NOT_READY"},
        {"START_UNKNOWN", "RTC_NOT_READY"},
        {"CLEANUP_PENDING", "RTC_NOT_READY"},
        {"CLEANUP_AUTH_REJECTED", "RTC_NOT_READY"},
        {"STORAGE_ERROR", "STORAGE_ERROR"},
        {"CONVERSATION_START_FAILED", "CONVERSATION_START_FAILED"},
    };
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        reset();
        active();
        /* begin_stop publishes cleanup before invoking the local stop callback. */
        assert(mybot_state_model_set_device_state(&runtime.state_model,
                                                  MYBOT_DEVICE_STATE_CLEANUP_PENDING));
        snprintf(runtime.lifecycle.error_code, sizeof(runtime.lifecycle.error_code), "%s",
                 errors[i].before);
        rtc_fini_result = -1;
        fail_end_session = true;
        dev_on_conversation_stop(&runtime);
        assert(rtc_finalizations == 1 && session_ends == 1);
        assert(runtime.media_cancel_requested && runtime.media_recovery_required);
        assert(strcmp(runtime.lifecycle.error_code, errors[i].after) == 0);
    }
    tests++;
}

int main(void) {
    test_validation_and_language_table();
    test_copy_async_and_actual_volume();
    test_language_idle_and_busy();
    test_bounded_queue_and_enqueue_failure();
    test_try_lock_and_shutdown();
    test_old_generation_does_not_consume_new_command();
    test_pause_only_active_and_actual_state();
    test_media_recovery_before_cloud_start();
    test_each_session_releases_rtc_before_next_start();
    test_rtc_cleanup_failure_blocks_cloud_start();
    test_rtc_cleanup_failure_preserves_real_error();
    printf("translation UI control: %u bounded/asynchronous scenarios passed\n", tests);
    return 0;
}
