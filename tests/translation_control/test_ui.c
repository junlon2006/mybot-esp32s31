/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ATOMIC_READ_HOOK test_ui_atomic_read_hook
#include "../../components/mybot_stack/mybot_sdk/mybot/src/core/mybot_app.c"

#define runtime s_default_runtime

typedef struct {
    aosl_mpq_func_t function;
    uintptr_t argc;
    uintptr_t arguments[4];
} notification_t;
static notification_t notifications[32];
static unsigned notification_count, language_sets, volume_sets, pause_sets, renders, tests;
static unsigned conversation_starts, session_ends, rtc_finalizations, rtc_joins, mode_sets;
static unsigned conversation_stops;
static unsigned recovery_confirmations;
static bool fail_queue, fail_volume, fail_pause, fail_end_session, fail_mode, paused;
static bool fail_recovery;
static int hardware_volume, rtc_fini_result;
static mybot_lcd_content_t displayed;
static mybot_platform_descriptor_t platform;
static const mybot_audio_capture_ops_t usb_ops = {0};
static const mybot_audio_playback_ops_t usb_playback_ops = {0};
static bool fail_capture_source;
static unsigned capture_switches;
static bool stop_during_caption_wait;
enum { LATE_FAULT_NONE, LATE_FAULT_CAPTURE, LATE_FAULT_PLAYBACK };
static unsigned late_audio_fault;

void test_ui_atomic_read_hook(const aosl_atomic_t *value) {
    if (!late_audio_fault || value != &runtime.media.playback_fault)
        return;
    unsigned fault = late_audio_fault;
    late_audio_fault = LATE_FAULT_NONE;
    /* Complete a worker error after both preflight fault reads observed zero,
     * before control resets cancellation for its new start attempt. */
    aosl_atomic_set(fault == LATE_FAULT_CAPTURE ? &runtime.media.capture_fault
                                                : &runtime.media.playback_fault,
                    true);
    media_error(&runtime);
}

const mybot_platform_descriptor_t *mybot_platform_registry_get(void) {
    return &platform;
}
void mybot_media_pipeline_init(mybot_media_pipeline_t *pipeline) {
    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->initialized = true;
}
int mybot_media_pipeline_start_usb(mybot_media_pipeline_t *pipeline,
                                   const mybot_media_pipeline_callbacks_t *callbacks) {
    assert(pipeline == runtime.usb_media);
    capture_switches++;
    pipeline->cbs = *callbacks;
    return fail_capture_source ? -1 : 0;
}
int mybot_media_pipeline_stop(mybot_media_pipeline_t *pipeline) {
    assert(pipeline == runtime.usb_media);
    return fail_end_session ? -1 : 0;
}
int mybot_media_pipeline_quiesce(mybot_media_pipeline_t *pipeline) {
    pipeline->paused = true;
    return 0;
}
int mybot_media_pipeline_destroy(mybot_media_pipeline_t *pipeline) {
    assert(pipeline == runtime.usb_media);
    return 0;
}
void mybot_media_pipeline_set_rtc_connected(mybot_media_pipeline_t *pipeline, bool connected) {
    pipeline->rtc_connected = connected;
}
int mybot_agora_rtc_send_audio(mybot_translation_direction_t direction, const void *data,
                               size_t len) {
    (void)direction;
    (void)data;
    (void)len;
    return 0;
}
void mybot_device_lifecycle_notify_conversation_ended(mybot_device_lifecycle_t *lifecycle) {
    if (!aosl_atomic_read(&lifecycle->shutting_down) &&
        aosl_atomic_read(&lifecycle->state) == MYBOT_DEVICE_STATE_IN_CONVERSATION)
        lifecycle->stop_request = true;
}
void mybot_device_lifecycle_notify_credentials_expiring(mybot_device_lifecycle_t *lifecycle) {
    lifecycle->stop_request = true;
}
void mybot_media_pipeline_push_remote_audio(mybot_media_pipeline_t *pipeline, const void *data,
                                            size_t length) {
    (void)pipeline;
    (void)data;
    (void)length;
}

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
    if (stop_during_caption_wait) {
        assert(milliseconds == 1 && runtime.caption_inbox_gate);
        stop_during_caption_wait = false;
        on_key_event(MYBOT_KEY_EVENT_CONVERSATION_STOP, &runtime);
        runtime.caption_inbox_gate = 0;
        return;
    }
    assert(!"a noncontended UI command must never wait");
}
int aosl_mpq_queue(aosl_mpq_t target, aosl_mpq_t source, aosl_ref_t reference, const char *name,
                   aosl_mpq_func_t callback, uintptr_t argc, ...) {
    assert(target == runtime.control_mpq && source == AOSL_MPQ_INVALID);
    assert(reference == AOSL_REF_INVALID);
    assert((strcmp(name, "handle_ui_command") == 0 && argc == 2) ||
           (strcmp(name, "handle_key_event") == 0 && argc == 4));
    if (fail_queue)
        return -1;
    assert(notification_count < sizeof(notifications) / sizeof(notifications[0]));
    notification_t *notification = &notifications[notification_count++];
    notification->function = callback;
    notification->argc = argc;
    va_list arguments;
    va_start(arguments, argc);
    for (uintptr_t i = 0; i < argc; ++i)
        notification->arguments[i] = va_arg(arguments, uintptr_t);
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
int mybot_device_lifecycle_set_selection(mybot_device_lifecycle_t *lifecycle,
                                         mybot_translation_mode_t mode,
                                         mybot_audio_source_t audio_source, const char *source,
                                         const char *target) {
    assert(lifecycle == &runtime.lifecycle);
    mode_sets++;
    if (fail_mode || runtime_get_state(&runtime) != MYBOT_STATE_READY)
        return -1;
    lifecycle->mode = mode;
    lifecycle->audio_source = audio_source;
    if (source && target) {
        snprintf(lifecycle->source_language, sizeof(lifecycle->source_language), "%s", source);
        snprintf(lifecycle->target_language, sizeof(lifecycle->target_language), "%s", target);
    }
    return 0;
}
int mybot_device_lifecycle_confirm_start_recovery(mybot_device_lifecycle_t *lifecycle) {
    assert(lifecycle == &runtime.lifecycle);
    recovery_confirmations++;
    if (fail_recovery || lifecycle->state != MYBOT_DEVICE_STATE_START_UNKNOWN)
        return -1;
    lifecycle->pending_unknown = lifecycle->journal_present = lifecycle->cleanup_needed = false;
    lifecycle->error_code[0] = '\0';
    lifecycle->state = MYBOT_DEVICE_STATE_RUNTIME;
    assert(mybot_state_model_set_device_state(&runtime.state_model, MYBOT_DEVICE_STATE_RUNTIME));
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
    pipeline->capture_fault = false;
    pipeline->playback_fault = false;
    return 0;
}
int mybot_agora_rtc_fini(void) {
    rtc_finalizations++;
    return rtc_fini_result;
}
int mybot_media_pipeline_flush_session(mybot_media_pipeline_t *pipeline) {
    assert(pipeline == &runtime.media || (runtime.usb_media && pipeline == runtime.usb_media));
    return 0;
}
int mybot_agora_rtc_init(const char *app_id, const mybot_agora_rtc_callbacks_t *callbacks) {
    (void)app_id;
    assert(callbacks->user_data == &runtime);
    return 0;
}
int mybot_agora_rtc_join(const mybot_device_translation_route_t *routes, size_t count) {
    assert(routes && (count == 1 || count == 2));
    rtc_joins++;
    return 0;
}
void mybot_device_lifecycle_request_start(mybot_device_lifecycle_t *lifecycle) {
    assert(lifecycle == &runtime.lifecycle);
    conversation_starts++;
    lifecycle->conversation_requested = true;
}
void mybot_device_lifecycle_request_stop(mybot_device_lifecycle_t *lifecycle) {
    assert(lifecycle == &runtime.lifecycle);
    conversation_stops++;
    lifecycle->stop_request = true;
}
void mybot_device_lifecycle_request_pair(mybot_device_lifecycle_t *lifecycle) {
    assert(lifecycle == &runtime.lifecycle);
}
int mybot_device_lifecycle_cycle_language(mybot_device_lifecycle_t *lifecycle, bool source) {
    (void)source;
    assert(lifecycle == &runtime.lifecycle);
    return 0;
}
int mybot_media_pipeline_adjust_volume(mybot_media_pipeline_t *pipeline, int delta) {
    return mybot_media_pipeline_set_volume(pipeline, hardware_volume + delta);
}
int mybot_lcd_show_content(mybot_lcd_t *lcd, const mybot_lcd_content_t *content) {
    assert(lcd == &runtime.presenter.lcd);
    displayed = *content;
    renders++;
    return 0;
}

static void reset(void) {
    free(runtime.usb_media);
    runtime.usb_media = NULL;
    free(runtime.silent_audio);
    runtime.silent_audio = NULL;
    memset(&runtime, 0, sizeof(runtime));
    memset(&platform, 0, sizeof(platform));
    fail_capture_source = false;
    capture_switches = 0;
    stop_during_caption_wait = false;
    late_audio_fault = LATE_FAULT_NONE;
    memset(notifications, 0, sizeof(notifications));
    memset(&displayed, 0, sizeof(displayed));
    notification_count = language_sets = volume_sets = pause_sets = renders = 0;
    conversation_starts = conversation_stops = session_ends = rtc_finalizations = rtc_joins =
        mode_sets = 0;
    fail_queue = fail_volume = fail_pause = fail_end_session = fail_mode = paused = false;
    fail_recovery = false;
    recovery_confirmations = 0;
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
    runtime.lifecycle.state = MYBOT_DEVICE_STATE_RUNTIME;
    strcpy(runtime.lifecycle.source_language, "zh-CN");
    strcpy(runtime.lifecycle.target_language, "en-US");
}
static void dispatch(unsigned index) {
    assert(index < notification_count);
    notification_t notification = notifications[index];
    notification.function(NULL, NULL, notification.argc, notification.arguments);
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
    runtime.lifecycle.state = MYBOT_DEVICE_STATE_IN_CONVERSATION;
    runtime.media.rtc_connected = 1;
}
static void unknown_start(void) {
    runtime.lifecycle.state = MYBOT_DEVICE_STATE_START_UNKNOWN;
    runtime.lifecycle.pending_unknown = runtime.lifecycle.journal_present = true;
    runtime.lifecycle.cleanup_needed = true;
    strcpy(runtime.lifecycle.error_code, "START_UNKNOWN");
    assert(
        mybot_state_model_set_device_state(&runtime.state_model, MYBOT_DEVICE_STATE_START_UNKNOWN));
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
    command.type = MYBOT_UI_SET_MODE;
    command.value = -1;
    assert(mybot_ui_submit(&command) == -1);
    command.value = 2;
    assert(mybot_ui_submit(&command) == -1);
    command.type = MYBOT_UI_SET_DIRECTION;
    command.value = MYBOT_TRANSLATION_DIRECTION_NONE;
    assert(mybot_ui_submit(&command) == -1);
    command.value = 3;
    assert(mybot_ui_submit(&command) == -1);
    command.type = MYBOT_UI_CONFIRM_START_RECOVERY;
    command.value = 0;
    assert(mybot_ui_submit(&command) == -1);
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

static void test_unknown_recovery_is_explicit_async_and_acknowledged(void) {
    reset();
    unknown_start();
    runtime.media.running = true; /* Ordinary background MIC/AEC capture is allowed. */
    platform.audio_usb_capture = &usb_ops;
    platform.audio_usb_playback = &usb_playback_ops;
    runtime.usb_media = calloc(1, sizeof(*runtime.usb_media));
    assert(runtime.usb_media);
    runtime.usb_media->initialized = true; /* Idle USB resources are also harmless. */
    mybot_ui_command_t command = {.type = MYBOT_UI_CONFIRM_START_RECOVERY, .value = 1};
    assert(mybot_ui_submit(&command) == 0);
    command.value = 0;
    assert(!recovery_confirmations && runtime.lifecycle.pending_unknown);
    dispatch(0);
    assert(recovery_confirmations == 1 && !runtime.lifecycle.pending_unknown);
    assert(runtime_get_state(&runtime) == MYBOT_STATE_READY);
    assert(!strcmp(runtime.lifecycle.source_language, "zh-CN") && hardware_volume == 40);
    assert(!conversation_starts && runtime.presenter.selection_revision == 1);
    command.value = 1;
    assert(mybot_ui_submit(&command) == 0);
    dispatch(1); /* A duplicate confirmation is rejected after the first recovered. */
    assert(recovery_confirmations == 2 && runtime.presenter.selection_revision == 2);
    assert(!conversation_starts && runtime_get_state(&runtime) == MYBOT_STATE_READY);
    tests++;
}

static void test_unknown_recovery_rejects_active_local_routes(void) {
    for (unsigned blocked = 0; blocked < 5; ++blocked) {
        reset();
        unknown_start();
        if (blocked == 0)
            runtime.media.rtc_connected = true;
        else if (blocked == 1)
            runtime.meeting_active = true;
        else if (blocked == 2)
            runtime.rtc_media_state = 3;
        else {
            runtime.usb_media = calloc(1, sizeof(*runtime.usb_media));
            assert(runtime.usb_media);
            if (blocked == 3)
                runtime.usb_media->running = true;
            else
                runtime.usb_media->rtc_connected = true;
        }
        mybot_ui_command_t command = {.type = MYBOT_UI_CONFIRM_START_RECOVERY, .value = 1};
        assert(mybot_ui_submit(&command) == 0);
        dispatch(0);
        assert(!recovery_confirmations && runtime.lifecycle.pending_unknown);
        assert(!strcmp(runtime.lifecycle.error_code, "START_UNKNOWN"));
        assert(runtime.presenter.selection_revision == 1);
    }
    tests++;
}

static void test_unknown_recovery_rechecks_state_and_acknowledges_failure(void) {
    reset();
    unknown_start();
    mybot_ui_command_t command = {.type = MYBOT_UI_CONFIRM_START_RECOVERY, .value = 1};
    assert(mybot_ui_submit(&command) == 0);
    runtime.lifecycle.state = MYBOT_DEVICE_STATE_STARTING;
    dispatch(0);
    assert(recovery_confirmations == 1 && runtime.lifecycle.pending_unknown);
    assert(runtime.presenter.selection_revision == 1);
    unknown_start();
    fail_recovery = true;
    runtime.presenter.selection_revision = UINT32_MAX;
    assert(mybot_ui_submit(&command) == 0);
    dispatch(1);
    assert(recovery_confirmations == 2 && runtime.lifecycle.pending_unknown);
    assert(runtime.presenter.selection_revision == 1);
    assert(!strcmp(runtime.lifecycle.error_code, "START_UNKNOWN"));
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

static void test_idle_playback_fault_blocks_start_until_recovered(void) {
    reset();
    runtime.media.playback_fault = true; /* A prompt can fail without an active cloud session. */
    fail_end_session = true;
    control_start_conversation(&runtime);
    assert(session_ends == 1 && !conversation_starts && runtime.media.playback_fault);
    assert(runtime.media_recovery_required && !paused);
    fail_end_session = false;
    control_start_conversation(&runtime);
    assert(session_ends == 2 && conversation_starts == 1 && !runtime.media.playback_fault);
    assert(!runtime.media_recovery_required && !runtime.media_cancel_requested);
    tests++;
}

static void test_idle_capture_fault_blocks_start_until_recovered(void) {
    reset();
    runtime.media.capture_fault = true;
    fail_end_session = true;
    control_start_conversation(&runtime);
    assert(session_ends == 1 && !conversation_starts && runtime.media.capture_fault);
    assert(runtime.media_recovery_required && !paused);
    fail_end_session = false;
    control_start_conversation(&runtime);
    assert(session_ends == 2 && conversation_starts == 1 && !runtime.media.capture_fault);
    assert(!runtime.media_recovery_required && !runtime.media_cancel_requested);
    tests++;
}

static void complete_requested_start(void) {
    if (!runtime.lifecycle.conversation_requested)
        return;
    mybot_device_conversation_t conversation = {
        .mode = runtime.lifecycle.mode,
        .route_count = runtime.lifecycle.mode == MYBOT_TRANSLATION_LISTEN ? 1 : 2,
    };
    for (unsigned route = 0; route < conversation.route_count; ++route)
        snprintf(conversation.routes[route].rtc_agent_uid,
                 sizeof(conversation.routes[route].rtc_agent_uid), "test-agent-%u", route);
    active();
    dev_on_conversation_start(&conversation, &runtime);
}

static void test_late_physical_fault_blocks_start_until_recovered(void) {
    static const struct {
        mybot_translation_mode_t mode;
        mybot_audio_source_t source;
    } scenes[] = {
        {MYBOT_TRANSLATION_LISTEN, MYBOT_AUDIO_SOURCE_CODEC},
        {MYBOT_TRANSLATION_LISTEN_SPEAK, MYBOT_AUDIO_SOURCE_CODEC},
        {MYBOT_TRANSLATION_LISTEN_SPEAK, MYBOT_AUDIO_SOURCE_USB},
    };
    for (unsigned scene = 0; scene < sizeof(scenes) / sizeof(scenes[0]); ++scene) {
        for (unsigned fault = LATE_FAULT_CAPTURE; fault <= LATE_FAULT_PLAYBACK; ++fault) {
            reset();
            runtime.lifecycle.mode = scenes[scene].mode;
            runtime.lifecycle.audio_source = scenes[scene].source;
            platform.audio_usb_capture = &usb_ops;
            platform.audio_usb_playback = &usb_playback_ops;
            late_audio_fault = fault;
            control_start_conversation(&runtime);
            assert(!runtime.lifecycle.stop_request); /* An idle worker error cannot request STOP. */
            complete_requested_start();
            assert(late_audio_fault == LATE_FAULT_NONE);
            assert(!conversation_starts && !rtc_joins && !runtime.lifecycle.conversation_requested);
            assert(runtime.media_recovery_required && runtime.media_cancel_requested);
            assert(!strcmp(displayed.error_code, "MEDIA_NOT_READY"));
            assert(fault == LATE_FAULT_CAPTURE ? runtime.media.capture_fault
                                               : runtime.media.playback_fault);

            control_start_conversation(&runtime);
            assert(session_ends == 1 && conversation_starts == 1);
            assert(!runtime.media.capture_fault && !runtime.media.playback_fault);
            assert(!runtime.media_recovery_required && !runtime.media_cancel_requested);
            assert(!runtime.lifecycle.error_code[0]);
            complete_requested_start();
            assert(rtc_joins == 1);
            tests++;
        }
    }
    reset();
}

static void test_each_session_releases_rtc_before_next_start(void) {
    reset();
    active();
    strcpy(runtime.rtc_channel, "first-project-channel");
    strcpy(runtime.rtc_agent_uid[0], "first-project-agent");
    runtime.rtc_media_state = 7; /* A previous RTC CONNECTED is waiting for the control tick. */
    dev_on_conversation_stop(&runtime);
    assert(rtc_finalizations == 1 && session_ends == 1);
    assert(!runtime.rtc_media_state);
    assert(runtime.media_cancel_requested && !runtime.rtc_channel[0] &&
           !runtime.rtc_agent_uid[0][0]);
    assert(mybot_state_model_set_device_state(&runtime.state_model, MYBOT_DEVICE_STATE_RUNTIME));
    runtime.rtc_media_state = 9; /* New starts must also clear any retained ready snapshot. */
    control_start_conversation(&runtime);
    assert(rtc_finalizations == 2 && conversation_starts == 1);
    assert(!runtime.rtc_media_state);
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

static void submit_and_dispatch(const mybot_ui_command_t *command) {
    unsigned index = notification_count;
    assert(mybot_ui_submit(command) == 0);
    dispatch(index);
}
static void test_mode_readback_and_repeated_failure(void) {
    reset();
    mybot_ui_command_t command = {.type = MYBOT_UI_SET_MODE,
                                  .value = MYBOT_TRANSLATION_LISTEN_SPEAK};
    assert(mybot_ui_submit(&command) == 0);
    assert(runtime.lifecycle.mode == MYBOT_TRANSLATION_LISTEN &&
           !runtime.presenter.selection_revision);
    dispatch(0);
    assert(mode_sets == 1 && displayed.translation_mode == MYBOT_TRANSLATION_LISTEN_SPEAK);
    assert(displayed.input_direction == MYBOT_TRANSLATION_DIRECTION_NONE &&
           displayed.selection_revision == 1);
    assert(!strcmp(displayed.source_language, "zh-CN") &&
           !strcmp(displayed.target_language, "en-US"));
    command.value = MYBOT_TRANSLATION_LISTEN;
    assert(mybot_ui_submit(&command) == 0);
    active(); /* A mode change must still be rejected if the session started while queued. */
    dispatch(1);
    assert(displayed.translation_mode == MYBOT_TRANSLATION_LISTEN_SPEAK &&
           displayed.selection_revision == 2);
    assert(!strcmp(displayed.error_code, "MODE_CHANGE_FAILED"));
    submit_and_dispatch(&command);
    assert(displayed.selection_revision == 3 &&
           !strcmp(displayed.error_code, "MODE_CHANGE_FAILED"));
    assert(mybot_state_model_set_device_state(&runtime.state_model, MYBOT_DEVICE_STATE_RUNTIME));
    submit_and_dispatch(&command);
    assert(displayed.translation_mode == MYBOT_TRANSLATION_LISTEN &&
           displayed.selection_revision == 4);
    assert(!displayed.error_code[0]);
    fail_mode = true;
    command.value = MYBOT_TRANSLATION_LISTEN_SPEAK;
    submit_and_dispatch(&command);
    submit_and_dispatch(&command);
    assert(displayed.selection_revision == 6 &&
           displayed.translation_mode == MYBOT_TRANSLATION_LISTEN);
    assert(!strcmp(displayed.error_code, "MODE_CHANGE_FAILED"));
    fail_queue = true;
    assert(mybot_ui_submit(&command) < 0 && displayed.selection_revision == 6);
    fail_queue = fail_mode = false;
    command.value = MYBOT_TRANSLATION_LISTEN;
    submit_and_dispatch(&command); /* Same mode also acknowledges completion. */
    assert(displayed.selection_revision == 7 && !displayed.error_code[0]);
    tests++;
}
static void test_direction_readback_and_independent_revisions(void) {
    reset();
    active();
    runtime.lifecycle.mode = MYBOT_TRANSLATION_LISTEN_SPEAK;
    mybot_ui_command_t command = {.type = MYBOT_UI_SET_PAUSED, .value = 0};
    submit_and_dispatch(&command);
    assert(!pause_sets && !displayed.selection_revision &&
           displayed.input_direction == MYBOT_TRANSLATION_DIRECTION_NONE);
    command.type = MYBOT_UI_SET_DIRECTION;
    command.value = MYBOT_TRANSLATION_DIRECTION_LISTEN;
    assert(mybot_ui_submit(&command) == 0);
    assert(runtime.input_direction == MYBOT_TRANSLATION_DIRECTION_NONE);
    dispatch(1);
    assert(pause_sets == 2 && !displayed.paused && runtime.presenter.media_ready);
    assert(displayed.input_direction == MYBOT_TRANSLATION_DIRECTION_LISTEN &&
           displayed.selection_revision == 1);
    command.value = MYBOT_TRANSLATION_DIRECTION_SPEAK;
    fail_pause = true;
    submit_and_dispatch(&command);
    assert(displayed.input_direction == MYBOT_TRANSLATION_DIRECTION_LISTEN &&
           displayed.selection_revision == 2);
    assert(!strcmp(displayed.error_code, "DIRECTION_CHANGE_FAILED"));
    submit_and_dispatch(&command);
    assert(displayed.selection_revision == 3 &&
           !strcmp(displayed.error_code, "DIRECTION_CHANGE_FAILED"));
    mybot_ui_command_t other = volume(66);
    submit_and_dispatch(&other);
    assert(displayed.volume == 66 && displayed.selection_revision == 3);
    other = languages("ja-JP", "ko-KR");
    submit_and_dispatch(&other); /* Busy language rejection cannot acknowledge a direction. */
    assert(displayed.selection_revision == 3);
    runtime.captions = mybot_captions_create();
    assert(runtime.captions);
    mybot_captions_reset(runtime.captions, 12);
    const char *json =
        "{\"object\":\"user.transcription\",\"stream_id\":1,\"turn_id\":1,\"text\":\"Hello\"}";
    assert(mybot_captions_apply_json(runtime.captions, MYBOT_TRANSLATION_DIRECTION_LISTEN, json,
                                     strlen(json)) == 1);
    refresh_caption_metadata(&runtime);
    mybot_presenter_render_state(&runtime.presenter, &runtime.state_model);
    assert(displayed.caption_count == 1 && displayed.caption_revision > 0);
    assert(displayed.selection_revision == 3 &&
           !strcmp(displayed.error_code, "DIRECTION_CHANGE_FAILED"));
    mybot_captions_destroy(runtime.captions);
    runtime.captions = NULL;
    fail_pause = false;
    submit_and_dispatch(&command);
    assert(displayed.input_direction == MYBOT_TRANSLATION_DIRECTION_SPEAK &&
           displayed.selection_revision == 4);
    assert(!displayed.error_code[0]);
    unsigned previous_pause_sets = pause_sets;
    runtime.lifecycle.stop_request = 1;
    command.value = MYBOT_TRANSLATION_DIRECTION_LISTEN;
    submit_and_dispatch(&command);
    assert(pause_sets == previous_pause_sets && displayed.selection_revision == 5);
    assert(displayed.input_direction == MYBOT_TRANSLATION_DIRECTION_SPEAK &&
           !displayed.error_code[0]);
    runtime.lifecycle.stop_request = 0;
    runtime.presenter.selection_revision = UINT32_MAX;
    command.value = MYBOT_TRANSLATION_DIRECTION_SPEAK;
    submit_and_dispatch(&command);
    assert(displayed.selection_revision == 1); /* Zero remains the initial no-completion marker. */
    tests++;
}

static void test_usb_selection_and_start_preflight(void) {
    reset();
    mybot_ui_command_t command = {.type = MYBOT_UI_SET_MODE,
                                  .value = MYBOT_TRANSLATION_LISTEN_SPEAK,
                                  .audio_source = MYBOT_AUDIO_SOURCE_USB};
    strcpy(command.source_language, "zh-CN");
    strcpy(command.target_language, "en-US");
    assert(mybot_ui_submit(&command) < 0); /* Both USB directions are required. */
    platform.audio_usb_capture = &usb_ops;
    assert(mybot_ui_submit(&command) < 0);
    platform.audio_usb_playback = &usb_playback_ops;
    command.value = MYBOT_TRANSLATION_LISTEN;
    assert(mybot_ui_submit(&command) < 0); /* Obsolete one-way meeting topology rejected. */
    command.value = MYBOT_TRANSLATION_LISTEN_SPEAK;
    submit_and_dispatch(&command);
    assert(displayed.audio_source == MYBOT_AUDIO_SOURCE_USB && displayed.usb_audio_available);
    assert(displayed.selection_revision == 1 && !strcmp(displayed.source_language, "zh-CN"));
    assert(!strcmp(displayed.target_language, "en-US"));
    fail_capture_source = true;
    control_start_conversation(&runtime);
    assert(conversation_starts == 0 && capture_switches == 1 && !runtime.usb_media);
    assert(!strcmp(displayed.error_code, "USB_AUDIO_NOT_READY"));
    fail_capture_source = false;
    control_start_conversation(&runtime);
    assert(conversation_starts == 1 && runtime.meeting_active && runtime.usb_media);
    active();
    command.audio_source = MYBOT_AUDIO_SOURCE_CODEC;
    submit_and_dispatch(&command);
    assert(displayed.audio_source == MYBOT_AUDIO_SOURCE_USB && displayed.selection_revision == 2);
    tests++;
}

static void test_unused_usb_capture_released_after_failed_start(void) {
    reset();
    platform.audio_usb_capture = &usb_ops;
    platform.audio_usb_playback = &usb_playback_ops;
    runtime.lifecycle.audio_source = MYBOT_AUDIO_SOURCE_USB;
    assert(start_usb_media(&runtime) == 0);
    runtime.lifecycle.conversation_requested = true;
    release_unclaimed_usb_media(&runtime);
    assert(session_ends == 0);
    runtime.lifecycle.conversation_requested = false;
    runtime.lifecycle.start_pending = true;
    release_unclaimed_usb_media(&runtime);
    assert(session_ends == 0);
    runtime.lifecycle.start_pending = false;
    runtime.lifecycle.local_media_active = true;
    release_unclaimed_usb_media(&runtime);
    assert(session_ends == 0);
    runtime.lifecycle.local_media_active = false;
    fail_end_session = true;
    release_unclaimed_usb_media(&runtime);
    assert(session_ends == 1 && runtime.media_recovery_required && runtime.usb_media);
    fail_end_session = false;
    release_unclaimed_usb_media(&runtime);
    assert(session_ends == 2 && !runtime.media_recovery_required && !runtime.usb_media);
    tests++;
}

static void test_key_queue_lifetime_and_valid_stop(void) {
    reset();
    active();
    s_lifecycle_gate = 1;
    on_key_event(MYBOT_KEY_EVENT_CONVERSATION_STOP, &runtime);
    assert(!runtime.media_cancel_requested && !notification_count);
    s_lifecycle_gate = 0;
    runtime.ui_gate = 1;
    on_key_event(MYBOT_KEY_EVENT_CONVERSATION_STOP, &runtime);
    assert(!runtime.media_cancel_requested && !notification_count);
    runtime.ui_gate = 0;
    runtime.control_mpq = AOSL_MPQ_INVALID;
    on_key_event(MYBOT_KEY_EVENT_CONVERSATION_STOP, &runtime);
    assert(!runtime.media_cancel_requested && !notification_count);
    runtime.control_mpq = 123;
    runtime.running = false;
    on_key_event(MYBOT_KEY_EVENT_CONVERSATION_STOP, &runtime);
    assert(!runtime.media_cancel_requested && !notification_count);
    runtime.running = true;
    on_key_event(MYBOT_KEY_EVENT_CONVERSATION_STOP, &runtime);
    assert(runtime.media_cancel_requested && notification_count == 1 && !conversation_stops);
    dispatch(0);
    assert(conversation_stops == 1 && runtime.lifecycle.stop_request);

    reset();
    on_key_event(MYBOT_KEY_EVENT_CONVERSATION_STOP, &runtime);
    assert(!runtime.media_cancel_requested && !notification_count);
    on_key_event(MYBOT_KEY_EVENT_VOLUME_UP, &runtime);
    assert(hardware_volume == 40 && notification_count == 1);
    dispatch(0);
    assert(hardware_volume == 50);
    tests++;
}

static void test_stale_key_stop_cannot_stop_next_session(void) {
    reset();
    control_start_conversation(&runtime);
    active();
    on_key_event(MYBOT_KEY_EVENT_CONVERSATION_STOP, &runtime);
    assert(notification_count == 1 && runtime.media_cancel_requested);
    uintptr_t old_attempt = notifications[0].arguments[3];
    dev_on_conversation_stop(&runtime);
    assert(mybot_state_model_set_device_state(&runtime.state_model, MYBOT_DEVICE_STATE_RUNTIME));
    control_start_conversation(&runtime);
    active();
    assert((uintptr_t)runtime.conversation_generation != old_attempt);
    assert(!runtime.media_cancel_requested && conversation_starts == 2);
    dispatch(0);
    assert(!conversation_stops && !runtime.media_cancel_requested);
    on_key_event(MYBOT_KEY_EVENT_CONVERSATION_STOP, &runtime);
    assert(runtime.media_cancel_requested);
    dispatch(1);
    assert(conversation_stops == 1);
    tests++;
}

static void test_key_runtime_generation_and_exit(void) {
    reset();
    on_key_event(MYBOT_KEY_EVENT_VOLUME_UP, &runtime);
    runtime.ui_generation++;
    dispatch(0);
    assert(hardware_volume == 40 && !volume_sets);
    s_lifecycle_gate = 1;
    on_key_event(MYBOT_KEY_EVENT_EXIT, &runtime);
    assert(!mybot_is_running() && notification_count == 1 && s_exit_generation == 1);
    s_lifecycle_gate = 0;

    reset();
    active();
    fail_queue = true;
    on_key_event(MYBOT_KEY_EVENT_CONVERSATION_STOP, &runtime);
    assert(runtime.media_cancel_requested && !mybot_is_running() && !notification_count);
    assert(!s_lifecycle_gate && !runtime.ui_gate);
    tests++;
}

static void test_stop_during_start_still_stops_active_session(void) {
    reset();
    control_start_conversation(&runtime);
    uintptr_t attempt = (uintptr_t)runtime.conversation_generation;
    assert(mybot_state_model_set_device_state(&runtime.state_model, MYBOT_DEVICE_STATE_STARTING));
    mybot_device_conversation_t conversation = {.mode = MYBOT_TRANSLATION_LISTEN, .route_count = 1};
    strcpy(conversation.routes[0].rtc_agent_uid, "listen-agent");
    /* Admit STOP after the cloud result's cancellation check but before its
     * local RTC setup, using the existing caption fence as the interleaving. */
    runtime.caption_inbox_gate = 1;
    stop_during_caption_wait = true;
    dev_on_conversation_start(&conversation, &runtime);
    assert(!stop_during_caption_wait && runtime.media_cancel_requested);
    assert(notification_count == 1 && notifications[0].arguments[3] == attempt);
    assert((uintptr_t)runtime.conversation_generation == attempt);
    active();
    dispatch(0);
    assert(conversation_stops == 1 && runtime.lifecycle.stop_request);
    tests++;
}

int main(void) {
    test_unknown_recovery_is_explicit_async_and_acknowledged();
    test_unknown_recovery_rejects_active_local_routes();
    test_unknown_recovery_rechecks_state_and_acknowledges_failure();
    test_key_queue_lifetime_and_valid_stop();
    test_stale_key_stop_cannot_stop_next_session();
    test_key_runtime_generation_and_exit();
    test_stop_during_start_still_stops_active_session();
    test_unused_usb_capture_released_after_failed_start();
    test_usb_selection_and_start_preflight();
    test_mode_readback_and_repeated_failure();
    test_direction_readback_and_independent_revisions();
    test_validation_and_language_table();
    test_copy_async_and_actual_volume();
    test_language_idle_and_busy();
    test_bounded_queue_and_enqueue_failure();
    test_try_lock_and_shutdown();
    test_old_generation_does_not_consume_new_command();
    test_pause_only_active_and_actual_state();
    test_media_recovery_before_cloud_start();
    test_idle_playback_fault_blocks_start_until_recovered();
    test_idle_capture_fault_blocks_start_until_recovered();
    test_late_physical_fault_blocks_start_until_recovered();
    test_each_session_releases_rtc_before_next_start();
    test_rtc_cleanup_failure_blocks_cloud_start();
    test_rtc_cleanup_failure_preserves_real_error();
    printf("translation UI control: %u bounded/asynchronous scenarios passed\n", tests);
    return 0;
}
