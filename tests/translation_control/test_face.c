/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise production route selection and callbacks. The established pause-fence
 * implementation is covered by tests/media_pause; this suite checks its use. */
#include "../../components/mybot_stack/mybot_sdk/mybot/src/core/mybot_app.c"

#define runtime s_default_runtime
#define FRAME_SAMPLES (MYBOT_MEDIA_FRAME_SAMPLES * (MYBOT_CLOUD_AEC ? 2U : 1U))
static unsigned tests, allocations, sends, pushes, accepted_audio, pause_calls;
static unsigned flushes, rtc_inits, rtc_joins, rtc_finalizations, ends, cleanup_requests;
static unsigned sleeps, unlock_after, stop_after_sleep;
static bool fail_allocation, fail_flush, fail_pause, fail_resume, stop_on_pause;
static bool cancel_after_send, inject_remote_during_fence;
static bool fail_usb_stop, fail_rtc_fini;
static unsigned usb_pushes, usb_stops, usb_destroys, quiesces;
static bool fail_physical_quiesce, fail_usb_quiesce, fail_physical_end;
static unsigned physical_device_pcm, usb_device_pcm, expected_stop_quiesces;
static unsigned physical_buffered_pcm, usb_buffered_pcm;
static bool fail_usb_flush, flush_hook_on_usb;
static void (*flush_hook)(void);
enum {
    READY_CANCELLED,
    READY_STOP_REQUESTED,
    READY_OFFLINE,
    READY_NETWORK_LOSS,
    READY_CAPTURE_FAULT,
    READY_PLAYBACK_FAULT,
    READY_RUNTIME_EXIT,
    READY_INVALIDATION_COUNT
};
static unsigned ready_invalidation;
static const mybot_audio_capture_ops_t usb_capture_ops = {0};
static const mybot_audio_playback_ops_t usb_playback_ops = {0};
static int fail_send_direction, fake_pending_pcm;
static int16_t uplink[FRAME_SAMPLES], sent_pcm[2][FRAME_SAMPLES];
static mybot_translation_direction_t sent_direction[2], direction_at_pause[16];
static bool pause_values[16];
static mybot_agora_rtc_callbacks_t registered;
static mybot_device_conversation_t conversation;

void test_log(const char *format, ...) {
    (void)format;
}
static mybot_platform_descriptor_t platform;
const mybot_platform_descriptor_t *mybot_platform_registry_get(void) {
    return &platform;
}

void *aosl_hal_malloc(size_t size) {
    if (fail_allocation)
        return NULL;
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
void aosl_msleep(unsigned milliseconds) {
    assert(milliseconds == 1 && runtime.remote_route_gate);
    sleeps++;
    if (sleeps == unlock_after)
        runtime.remote_route_gate = 0;
    if (sleeps == stop_after_sleep)
        runtime.lifecycle.stop_request = 1;
}
int mybot_agora_rtc_send_audio(mybot_translation_direction_t direction, const void *data,
                               size_t length) {
    assert(sends < 2 && data && length == sizeof(uplink));
    sent_direction[sends] = direction;
    memcpy(sent_pcm[sends++], data, length);
    if (cancel_after_send)
        runtime.media_cancel_requested = 1;
    return (int)direction == fail_send_direction ? -1 : 0;
}
void mybot_media_pipeline_push_remote_audio(mybot_media_pipeline_t *pipeline, const void *data,
                                            size_t length) {
    assert((pipeline == &runtime.media || pipeline == runtime.usb_media) && data &&
           length == MYBOT_MEDIA_FRAME_BYTES);
    if (pipeline == runtime.usb_media)
        usb_pushes++;
    pushes++;
    if (pipeline->rtc_connected && !pipeline->paused) {
        accepted_audio++;
        fake_pending_pcm++;
    }
}
bool mybot_media_pipeline_is_paused(const mybot_media_pipeline_t *pipeline) {
    assert(pipeline == &runtime.media || pipeline == runtime.usb_media);
    return pipeline->paused != 0;
}
static void remote(mybot_translation_direction_t direction) {
    int16_t pcm[MYBOT_MEDIA_FRAME_SAMPLES] = {123};
    rtc_on_remote_audio(direction, 900, pcm, sizeof(pcm), &runtime);
}
int mybot_media_pipeline_set_paused(mybot_media_pipeline_t *pipeline, bool paused) {
    assert((pipeline == &runtime.media || pipeline == runtime.usb_media) &&
           runtime.remote_route_gate);
    assert(pause_calls < sizeof(pause_values) / sizeof(pause_values[0]));
    direction_at_pause[pause_calls] = runtime.input_direction;
    pause_values[pause_calls++] = paused;
    pipeline->paused = 1;
    if (inject_remote_during_fence) {
        unsigned before = pushes;
        remote(MYBOT_TRANSLATION_DIRECTION_LISTEN);
        remote(MYBOT_TRANSLATION_DIRECTION_SPEAK);
        assert(pushes == before); /* Callback admission stays closed throughout the fence. */
    }
    if ((paused && fail_pause) || (!paused && fail_resume))
        return -1;
    fake_pending_pcm = 0;
    if (paused && stop_on_pause)
        runtime.media_cancel_requested = 1;
    if (!paused)
        pipeline->paused = 0;
    return 0;
}
int mybot_media_pipeline_flush_session(mybot_media_pipeline_t *pipeline) {
    assert(pipeline == &runtime.media || pipeline == runtime.usb_media);
    assert(!runtime.ui_gate && !runtime.media.rtc_connected);
    assert(!runtime.usb_media || !runtime.usb_media->rtc_connected);
    flushes++;
    if (fail_flush || (pipeline == runtime.usb_media && fail_usb_flush))
        return -1;
    if (pipeline == &runtime.media)
        physical_buffered_pcm = 0;
    else
        usb_buffered_pcm = 0;
    if (flush_hook && (pipeline == runtime.usb_media) == flush_hook_on_usb) {
        void (*hook)(void) = flush_hook;
        flush_hook = NULL;
        hook();
    }
    return 0;
}
int mybot_media_pipeline_end_session(mybot_media_pipeline_t *pipeline) {
    assert(pipeline == &runtime.media);
    ends++;
    pipeline->rtc_connected = false;
    if (fail_physical_end)
        return -1;
    if (pipeline->playback_suspended) {
        /* Session recovery retries a failed quiesce's device stop before restart. */
        physical_device_pcm = 0;
        pipeline->playback_suspended = false;
    }
    pipeline->rtc_connected = pipeline->paused = 0;
    fake_pending_pcm = 0;
    return 0;
}
void mybot_media_pipeline_set_rtc_connected(mybot_media_pipeline_t *pipeline, bool connected) {
    assert(pipeline == &runtime.media || pipeline == runtime.usb_media);
    if (connected) {
        assert(runtime.ui_gate);
        assert(!physical_buffered_pcm && !usb_buffered_pcm);
    }
    pipeline->rtc_connected = connected;
}
void mybot_device_lifecycle_notify_conversation_ended(mybot_device_lifecycle_t *lifecycle) {
    assert(lifecycle == &runtime.lifecycle);
    cleanup_requests++;
    /* Preserve the parent ID for the real lifecycle's asynchronous HTTP stop. */
    assert(strcmp(lifecycle->conversation_id, "face-parent") == 0);
    lifecycle->stop_request = 1;
}
void mybot_device_lifecycle_notify_credentials_expiring(mybot_device_lifecycle_t *lifecycle) {
    mybot_device_lifecycle_notify_conversation_ended(lifecycle);
}
int mybot_agora_rtc_init(const char *app_id, const mybot_agora_rtc_callbacks_t *callbacks) {
    assert(strcmp(app_id, "face-app") == 0 && callbacks->user_data == &runtime);
    registered = *callbacks;
    rtc_inits++;
    return 0;
}
int mybot_agora_rtc_join(const mybot_device_translation_route_t *routes, size_t count) {
    assert(routes == conversation.routes && count == conversation.route_count);
    rtc_joins++;
    return 0;
}
int mybot_agora_rtc_fini(void) {
    if (expected_stop_quiesces) {
        assert(quiesces == expected_stop_quiesces);
        if (!fail_physical_quiesce)
            assert(!physical_device_pcm);
        if (runtime.usb_media && runtime.usb_media->running && !fail_usb_quiesce)
            assert(!usb_device_pcm);
    }
    rtc_finalizations++;
    return fail_rtc_fini ? -1 : 0;
}
void mybot_media_pipeline_init(mybot_media_pipeline_t *pipeline) {
    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->initialized = true;
}
int mybot_media_pipeline_start_usb(mybot_media_pipeline_t *pipeline,
                                   const mybot_media_pipeline_callbacks_t *callbacks) {
    assert(pipeline == runtime.usb_media && pipeline->initialized);
    pipeline->cbs = *callbacks;
    pipeline->running = true;
    return 0;
}
int mybot_media_pipeline_quiesce(mybot_media_pipeline_t *pipeline) {
    assert(runtime.media_cancel_requested &&
           (pipeline == &runtime.media || pipeline == runtime.usb_media));
    quiesces++;
    pipeline->paused = true;
    pipeline->playback_suspended = true;
    if (pipeline == &runtime.media) {
        if (fail_physical_quiesce)
            return -1;
        physical_device_pcm = 0;
    } else {
        if (fail_usb_quiesce)
            return -1;
        usb_device_pcm = 0;
    }
    return 0;
}
int mybot_media_pipeline_stop(mybot_media_pipeline_t *pipeline) {
    assert(pipeline == runtime.usb_media);
    usb_stops++;
    pipeline->running = pipeline->rtc_connected = false;
    if (fail_usb_stop)
        return -1;
    usb_device_pcm = 0;
    return 0;
}
int mybot_media_pipeline_destroy(mybot_media_pipeline_t *pipeline) {
    assert(pipeline == runtime.usb_media && !pipeline->running);
    usb_destroys++;
    return 0;
}
int mybot_lcd_show_content(mybot_lcd_t *lcd, const mybot_lcd_content_t *content) {
    (void)lcd;
    (void)content;
    return 0;
}
void mybot_device_lifecycle_tick(mybot_device_lifecycle_t *lifecycle) {
    assert(lifecycle == &runtime.lifecycle);
}
int mybot_media_pipeline_get_volume(mybot_media_pipeline_t *pipeline, int *volume) {
    assert(pipeline == &runtime.media);
    *volume = 45;
    return 0;
}

static void tick_control(void) {
    uintptr_t argument = (uintptr_t)&runtime;
    control_tick_timer(AOSL_MPQ_TIMER_INVALID, NULL, 1, &argument);
}

static void reset(void) {
    aosl_hal_free(runtime.silent_audio);
    aosl_hal_free(runtime.usb_media);
    assert(!allocations);
    memset(&runtime, 0, sizeof(runtime));
    memset(&registered, 0, sizeof(registered));
    memset(&conversation, 0, sizeof(conversation));
    memset(sent_pcm, 0, sizeof(sent_pcm));
    memset(sent_direction, 0, sizeof(sent_direction));
    memset(pause_values, 0, sizeof(pause_values));
    memset(direction_at_pause, 0, sizeof(direction_at_pause));
    s_lifecycle_gate = s_exit_generation = s_run_exit_generation = 0;
    runtime.running = runtime.media.running = 1;
    runtime.lifecycle.network_available = true;
    runtime.lifecycle.local_media_active = true;
    runtime.lifecycle.mode = MYBOT_TRANSLATION_LISTEN_SPEAK;
    strcpy(runtime.lifecycle.conversation_id, "face-parent");
    conversation.mode = MYBOT_TRANSLATION_LISTEN_SPEAK;
    conversation.route_count = 2;
    for (unsigned i = 0; i < conversation.route_count; ++i) {
        mybot_device_translation_route_t *route = &conversation.routes[i];
        route->direction =
            i ? MYBOT_TRANSLATION_DIRECTION_SPEAK : MYBOT_TRANSLATION_DIRECTION_LISTEN;
        strcpy(route->rtc_app_id, "face-app");
        strcpy(route->rtc_channel, "face-channel");
        strcpy(route->rtc_agent_uid, i ? "speak-agent" : "listen-agent");
    }
    assert(mybot_state_model_begin_start(&runtime.state_model));
    assert(mybot_state_model_begin_services(&runtime.state_model));
    assert(mybot_state_model_services_ready(&runtime.state_model));
    assert(mybot_state_model_set_device_state(&runtime.state_model,
                                              MYBOT_DEVICE_STATE_IN_CONVERSATION));
    sends = pushes = accepted_audio = pause_calls = 0;
    flushes = rtc_inits = rtc_joins = rtc_finalizations = ends = cleanup_requests = 0;
    sleeps = unlock_after = stop_after_sleep = 0;
    fail_allocation = fail_flush = fail_pause = fail_resume = stop_on_pause = false;
    cancel_after_send = inject_remote_during_fence = false;
    fail_send_direction = fake_pending_pcm = 0;
    fail_usb_stop = fail_rtc_fini = false;
    usb_pushes = usb_stops = usb_destroys = quiesces = 0;
    fail_physical_quiesce = fail_usb_quiesce = fail_physical_end = false;
    physical_device_pcm = usb_device_pcm = expected_stop_quiesces = 0;
    physical_buffered_pcm = usb_buffered_pcm = 0;
    fail_usb_flush = flush_hook_on_usb = false;
    flush_hook = NULL;
    platform.audio_usb_capture = &usb_capture_ops;
    platform.audio_usb_playback = &usb_playback_ops;
    for (unsigned i = 0; i < FRAME_SAMPLES; ++i)
        uplink[i] = (int16_t)(i % 2 ? -1000 - (int)i : 1000 + (int)i);
}
static void start(void) {
    dev_on_conversation_start(&conversation, &runtime);
    assert(flushes == 1 && rtc_inits == 1 && rtc_joins == 1 && !cleanup_requests);
    assert(runtime.silent_audio && allocations == 1);
    assert(runtime.input_direction == MYBOT_TRANSLATION_DIRECTION_NONE);
    assert(runtime.audio_mode == MYBOT_TRANSLATION_LISTEN_SPEAK);
    registered.on_state_changed(MYBOT_RTC_STATE_CONNECTED, registered.user_data);
    assert(!runtime.media.rtc_connected);
    tick_control();
    assert(runtime.media.rtc_connected && flushes == 2);
}
static void expect_frames(mybot_translation_direction_t selected) {
    assert(sends == 2);
    assert(sent_direction[0] == MYBOT_TRANSLATION_DIRECTION_LISTEN);
    assert(sent_direction[1] == MYBOT_TRANSLATION_DIRECTION_SPEAK);
    for (unsigned route = 0; route < 2; ++route) {
        for (unsigned i = 0; i < FRAME_SAMPLES; ++i) {
            int16_t expected =
                sent_direction[route] == selected || (MYBOT_CLOUD_AEC && (i & 1U)) ? uplink[i] : 0;
            assert(sent_pcm[route][i] == expected);
        }
    }
}
static void test_initial_and_selected_uplink(void) {
    reset();
    start();
    assert(media_send_audio(uplink, sizeof(uplink), &runtime) == 0);
    expect_frames(MYBOT_TRANSLATION_DIRECTION_NONE);
    remote(MYBOT_TRANSLATION_DIRECTION_LISTEN);
    remote(MYBOT_TRANSLATION_DIRECTION_SPEAK);
    assert(!pushes);
    for (mybot_translation_direction_t selected = MYBOT_TRANSLATION_DIRECTION_LISTEN;
         selected <= MYBOT_TRANSLATION_DIRECTION_SPEAK; ++selected) {
        assert(control_set_direction(&runtime, selected) == 0);
        sends = 0;
        assert(media_send_audio(uplink, sizeof(uplink), &runtime) == 0);
        expect_frames(selected);
    }
    tests++;
}
static void test_media_single_route(void) {
    reset();
    conversation.mode = runtime.lifecycle.mode = MYBOT_TRANSLATION_LISTEN;
    conversation.route_count = 1;
    dev_on_conversation_start(&conversation, &runtime);
    assert(!runtime.silent_audio && !allocations);
    assert(runtime.input_direction == MYBOT_TRANSLATION_DIRECTION_LISTEN);
    assert(rtc_inits == 1 && rtc_joins == 1 && !cleanup_requests);
    assert(media_send_audio(uplink, sizeof(uplink), &runtime) == 0);
    assert(sends == 1 && sent_direction[0] == MYBOT_TRANSLATION_DIRECTION_LISTEN);
    assert(!memcmp(sent_pcm[0], uplink, sizeof(uplink)));
    assert(control_set_direction(&runtime, MYBOT_TRANSLATION_DIRECTION_SPEAK) < 0);
    assert(!pause_calls);
    tests++;
}
static void test_switch_fences_and_filters_audio(void) {
    reset();
    start();
    runtime.input_direction = MYBOT_TRANSLATION_DIRECTION_LISTEN;
    remote(MYBOT_TRANSLATION_DIRECTION_LISTEN);
    assert(accepted_audio == 1 && fake_pending_pcm == 1);
    inject_remote_during_fence = true;
    assert(control_set_direction(&runtime, MYBOT_TRANSLATION_DIRECTION_SPEAK) == 0);
    assert(pause_calls == 2 && pause_values[0] && !pause_values[1]);
    assert(direction_at_pause[0] == MYBOT_TRANSLATION_DIRECTION_LISTEN);
    assert(direction_at_pause[1] == MYBOT_TRANSLATION_DIRECTION_SPEAK);
    assert(!fake_pending_pcm && !runtime.media.paused && !runtime.remote_route_gate);
    remote(MYBOT_TRANSLATION_DIRECTION_LISTEN);
    assert(pushes == 1);
    remote(MYBOT_TRANSLATION_DIRECTION_SPEAK);
    assert(pushes == 2 && accepted_audio == 2);
    assert(control_set_direction(&runtime, MYBOT_TRANSLATION_DIRECTION_SPEAK) == 0);
    assert(pause_calls == 2); /* Re-selecting the active unpaused direction is idempotent. */
    runtime.media.paused = 1;
    assert(control_set_direction(&runtime, MYBOT_TRANSLATION_DIRECTION_SPEAK) == 0);
    assert(pause_calls == 4 && !runtime.media.paused);
    tests++;
}
static void test_switch_failure_and_stop(void) {
    for (unsigned failure = 0; failure < 3; ++failure) {
        reset();
        start();
        runtime.input_direction = MYBOT_TRANSLATION_DIRECTION_LISTEN;
        fail_pause = failure == 0;
        fail_resume = failure == 1;
        stop_on_pause = failure == 2;
        assert(control_set_direction(&runtime, MYBOT_TRANSLATION_DIRECTION_SPEAK) < 0);
        assert(runtime.input_direction == MYBOT_TRANSLATION_DIRECTION_LISTEN);
        assert(runtime.media.paused && !runtime.remote_route_gate);
        assert(pause_calls == (failure == 1 ? 2U : 1U));
        remote(MYBOT_TRANSLATION_DIRECTION_SPEAK);
        remote(MYBOT_TRANSLATION_DIRECTION_LISTEN);
        assert(!accepted_audio);
        if (failure == 2) {
            assert(media_send_audio(uplink, sizeof(uplink), &runtime) == 0 && !sends);
        }
        tests++;
    }
}
static void test_remote_gate_contention(void) {
    reset();
    start();
    runtime.input_direction = MYBOT_TRANSLATION_DIRECTION_LISTEN;
    runtime.remote_route_gate = 1;
    remote(MYBOT_TRANSLATION_DIRECTION_LISTEN);
    assert(!pushes && !sleeps); /* RTC callback never waits. */
    assert(control_set_direction(&runtime, MYBOT_TRANSLATION_DIRECTION_SPEAK) < 0);
    assert(sleeps == 49 && !pause_calls && runtime.remote_route_gate);
    assert(runtime.input_direction == MYBOT_TRANSLATION_DIRECTION_LISTEN);
    sleeps = 0;
    unlock_after = 2;
    assert(control_set_direction(&runtime, MYBOT_TRANSLATION_DIRECTION_SPEAK) == 0);
    assert(sleeps == 2 && pause_calls == 2 && !runtime.remote_route_gate);
    reset();
    start();
    runtime.remote_route_gate = 1;
    stop_after_sleep = 1;
    unlock_after = 2;
    assert(control_set_direction(&runtime, MYBOT_TRANSLATION_DIRECTION_LISTEN) < 0);
    assert(!pause_calls && !runtime.remote_route_gate);
    assert(runtime.input_direction == MYBOT_TRANSLATION_DIRECTION_NONE);
    tests++;
}
static void test_start_failure_requests_parent_cleanup(void) {
    for (unsigned failure = 0; failure < 2; ++failure) {
        reset();
        fail_allocation = failure == 0;
        fail_flush = failure == 1;
        dev_on_conversation_start(&conversation, &runtime);
        assert(cleanup_requests == 1 && runtime.lifecycle.stop_request);
        assert(!rtc_inits && !rtc_joins && !runtime.silent_audio && !allocations);
        assert(strcmp(runtime.lifecycle.conversation_id, "face-parent") == 0);
        if (!failure)
            assert(strcmp(runtime.lifecycle.error_code, "MEDIA_NOT_READY") == 0);
        /* The lifecycle owns HTTP stop; exercise the app's local cleanup callback. */
        dev_on_conversation_stop(&runtime);
        assert(rtc_finalizations == 1 && ends == 1 && runtime.media_cancel_requested);
        assert(runtime.input_direction == MYBOT_TRANSLATION_DIRECTION_NONE);
        tests++;
    }
}
static void test_uplink_failure_cancel_and_invalid_frame(void) {
    reset();
    start();
    assert(media_send_audio(NULL, sizeof(uplink), &runtime) < 0);
    assert(media_send_audio(uplink, sizeof(uplink) - 2, &runtime) < 0 && !sends);
    fail_send_direction = MYBOT_TRANSLATION_DIRECTION_LISTEN;
    assert(media_send_audio(uplink, sizeof(uplink), &runtime) < 0 && sends == 2);
    sends = 0;
    cancel_after_send = true;
    fail_send_direction = 0;
    assert(media_send_audio(uplink, sizeof(uplink), &runtime) == 0 && sends == 1);
    sends = 0;
    assert(media_send_audio(uplink, sizeof(uplink), &runtime) == 0 && !sends);
    tests++;
}
static void start_meeting(void) {
    reset();
    runtime.lifecycle.audio_source = MYBOT_AUDIO_SOURCE_USB;
    assert(start_usb_media(&runtime) == 0);
    assert(runtime.usb_media && allocations == 1 && runtime.meeting_active);
    dev_on_conversation_start(&conversation, &runtime);
    assert(flushes == 2 && rtc_inits == 1 && rtc_joins == 1 && !cleanup_requests);
    assert(!runtime.silent_audio && runtime.input_direction == MYBOT_TRANSLATION_DIRECTION_NONE);
    assert(!runtime.media.rtc_connected && !runtime.usb_media->rtc_connected);
    registered.on_state_changed(MYBOT_RTC_STATE_CONNECTED, registered.user_data);
    assert(!runtime.media.rtc_connected && !runtime.usb_media->rtc_connected);
    tick_control();
    assert(runtime.media.rtc_connected && runtime.usb_media->rtc_connected && flushes == 4);
}
static void test_meeting_routes_remain_independent(void) {
    start_meeting();
    assert(control_set_direction(&runtime, MYBOT_TRANSLATION_DIRECTION_SPEAK) < 0);
    assert(!pause_calls);
    assert(media_send_audio(uplink, sizeof(uplink), &runtime) == 0);
    int16_t usb_pcm[FRAME_SAMPLES];
    for (unsigned i = 0; i < FRAME_SAMPLES; ++i)
        usb_pcm[i] = MYBOT_CLOUD_AEC && (i & 1U) ? 0 : 2345;
    assert(usb_media_send_audio(usb_pcm, sizeof(usb_pcm), &runtime) == 0);
    assert(sends == 2 && sent_direction[0] == MYBOT_TRANSLATION_DIRECTION_SPEAK &&
           sent_direction[1] == MYBOT_TRANSLATION_DIRECTION_LISTEN);
    assert(!memcmp(sent_pcm[0], uplink, sizeof(uplink)));
    assert(!memcmp(sent_pcm[1], usb_pcm, sizeof(usb_pcm)));
    remote(MYBOT_TRANSLATION_DIRECTION_LISTEN);
    assert(pushes == 1 && !usb_pushes);
    remote(MYBOT_TRANSLATION_DIRECTION_SPEAK);
    assert(pushes == 2 && usb_pushes == 1 && accepted_audio == 2);
    runtime.remote_route_gate = 1;
    remote(MYBOT_TRANSLATION_DIRECTION_SPEAK);
    assert(pushes == 2 && !sleeps);
    runtime.remote_route_gate = 0;
    sends = 0;
    runtime.meeting_control_busy = 1;
    assert(media_send_audio(uplink, sizeof(uplink), &runtime) == 0);
    assert(usb_media_send_audio(usb_pcm, sizeof(usb_pcm), &runtime) == 0 && !sends);
    runtime.meeting_control_busy = 0;
    dev_on_conversation_stop(&runtime);
    assert(!runtime.usb_media && !runtime.meeting_active && !allocations);
    assert(usb_stops == 1 && usb_destroys == 1 && quiesces == 2);
    tests++;
}
static void test_meeting_pause_failure_and_disconnect(void) {
    start_meeting();
    inject_remote_during_fence = true;
    assert(control_set_paused(&runtime, true) == 0);
    assert(runtime.media.paused && runtime.usb_media->paused && !runtime.remote_route_gate);
    assert(control_set_paused(&runtime, false) == 0);
    assert(!runtime.media.paused && !runtime.usb_media->paused);
    fail_resume = true;
    assert(control_set_paused(&runtime, false) < 0);
    assert(runtime.media.paused && runtime.usb_media->paused && !runtime.meeting_control_busy);
    runtime.usb_media->cbs.on_audio_error(runtime.usb_media->cbs.user_data);
    assert(runtime.media_cancel_requested && runtime.lifecycle.stop_request &&
           cleanup_requests == 1);
    assert(!runtime.media.rtc_connected);
    remote(MYBOT_TRANSLATION_DIRECTION_LISTEN);
    remote(MYBOT_TRANSLATION_DIRECTION_SPEAK);
    assert(!pushes && !accepted_audio);
    unsigned previous_sends = sends;
    assert(usb_media_send_audio(uplink, sizeof(uplink), &runtime) == 0);
    assert(media_send_audio(uplink, sizeof(uplink), &runtime) == 0 && sends == previous_sends);
    dev_on_conversation_stop(&runtime);
    assert(!runtime.usb_media && !allocations && quiesces == 2);
    tests++;
}
static void test_physical_audio_error_closes_session_media(void) {
    for (unsigned bidirectional = 0; bidirectional < 2; ++bidirectional) {
        reset();
        if (!bidirectional) {
            runtime.lifecycle.mode = conversation.mode = MYBOT_TRANSLATION_LISTEN;
            conversation.route_count = 1;
        }
        dev_on_conversation_start(&conversation, &runtime);
        registered.on_state_changed(MYBOT_RTC_STATE_CONNECTED, registered.user_data);
        tick_control();
        assert(runtime.media.rtc_connected);
        media_error(&runtime);
        assert(runtime.media_cancel_requested && !runtime.media.rtc_connected);
        assert(runtime.lifecycle.stop_request && cleanup_requests == 1);
        remote(MYBOT_TRANSLATION_DIRECTION_LISTEN);
        remote(MYBOT_TRANSLATION_DIRECTION_SPEAK);
        assert(!pushes && !accepted_audio);
        dev_on_conversation_stop(&runtime);
        assert(ends == 1 && rtc_finalizations == 1 && !runtime.media_recovery_required);
    }
    tests++;
}

static void test_meeting_teardown_retains_pending_workers(void) {
    for (unsigned failure = 0; failure < 2; ++failure) {
        start_meeting();
        mybot_media_pipeline_t *retained = runtime.usb_media;
        fail_usb_stop = failure == 0;
        fail_rtc_fini = failure == 1;
        dev_on_conversation_stop(&runtime);
        assert(runtime.usb_media == retained && runtime.media_recovery_required &&
               allocations == 1);
        assert(!runtime.meeting_active && !usb_destroys);
        remote(MYBOT_TRANSLATION_DIRECTION_SPEAK);
        assert(!pushes);
        fail_usb_stop = fail_rtc_fini = false;
        dev_on_conversation_stop(&runtime);
        assert(!runtime.usb_media && !runtime.media_recovery_required && !allocations);
        assert(usb_destroys == 1);
        tests++;
    }
}

static void start_codec_translation(bool bidirectional) {
    reset();
    if (!bidirectional) {
        runtime.lifecycle.mode = conversation.mode = MYBOT_TRANSLATION_LISTEN;
        conversation.route_count = 1;
    }
    dev_on_conversation_start(&conversation, &runtime);
    registered.on_state_changed(MYBOT_RTC_STATE_CONNECTED, registered.user_data);
    assert(!runtime.media.rtc_connected);
    tick_control();
    assert(runtime.media.rtc_connected);
}

static void start_ready_scene(unsigned scene) {
    if (scene == 2)
        start_meeting();
    else
        start_codec_translation(scene == 1);
}

static void test_ready_waits_for_control_flush(void) {
    for (unsigned scene = 0; scene < 3; ++scene) {
        start_ready_scene(scene);
        unsigned previous_flushes = flushes;
        physical_buffered_pcm = 960;
        usb_buffered_pcm = scene == 2 ? 960 : 0;
        registered.on_state_changed(MYBOT_RTC_STATE_RECONNECTING, registered.user_data);
        registered.on_state_changed(MYBOT_RTC_STATE_CONNECTED, registered.user_data);
        assert(!runtime.media.rtc_connected);
        assert(!runtime.usb_media || !runtime.usb_media->rtc_connected);
        assert(flushes == previous_flushes);
        tick_control();
        assert(runtime.media.rtc_connected);
        assert(!runtime.usb_media || runtime.usb_media->rtc_connected);
        assert(!physical_buffered_pcm && !usb_buffered_pcm && !runtime.ui_gate);
        assert(flushes == previous_flushes + (scene == 2 ? 2U : 1U));
        previous_flushes = flushes;
        tick_control();
        assert(flushes == previous_flushes);
        tests++;
    }
}

static void reconnect_during_flush(void) {
    registered.on_state_changed(MYBOT_RTC_STATE_RECONNECTING, registered.user_data);
    registered.on_state_changed(MYBOT_RTC_STATE_CONNECTED, registered.user_data);
}

static void fail_rtc_during_flush(void) {
    registered.on_state_changed(MYBOT_RTC_STATE_ERROR, registered.user_data);
}

static void test_ready_rejects_changed_rtc_epoch(void) {
    for (unsigned meeting = 0; meeting < 2; ++meeting) {
        for (unsigned terminal = 0; terminal < 2; ++terminal) {
            start_ready_scene(meeting ? 2 : 1);
            reconnect_during_flush();
            unsigned previous_flushes = flushes;
            flush_hook_on_usb = meeting != 0;
            flush_hook = terminal ? fail_rtc_during_flush : reconnect_during_flush;
            tick_control();
            assert(!runtime.media.rtc_connected);
            assert(!runtime.usb_media || !runtime.usb_media->rtc_connected);
            assert(flushes == previous_flushes + (meeting ? 2U : 1U));
            assert(!flush_hook && !runtime.ui_gate);
            previous_flushes = flushes;
            tick_control();
            assert(runtime.media.rtc_connected == !terminal);
            assert(!runtime.usb_media || runtime.usb_media->rtc_connected == !terminal);
            assert(flushes == previous_flushes + (terminal ? 0U : (meeting ? 2U : 1U)));
            tests++;
        }
    }
}

static void invalidate_ready_during_flush(void) {
    switch (ready_invalidation) {
    case READY_CANCELLED:
        runtime.media_cancel_requested = true;
        break;
    case READY_STOP_REQUESTED:
        runtime.lifecycle.stop_request = true;
        break;
    case READY_OFFLINE:
        runtime.lifecycle.network_available = false;
        break;
    case READY_NETWORK_LOSS:
        runtime.lifecycle.network_loss_pending = true;
        break;
    case READY_CAPTURE_FAULT:
        runtime.media.capture_fault = true;
        break;
    case READY_PLAYBACK_FAULT:
        runtime.media.playback_fault = true;
        break;
    case READY_RUNTIME_EXIT:
        runtime_publish_exit(&runtime);
        break;
    }
}

static void test_ready_rechecks_cancellation_after_flush(void) {
    for (ready_invalidation = 0; ready_invalidation < READY_INVALIDATION_COUNT;
         ++ready_invalidation) {
        start_ready_scene(ready_invalidation & 1U ? 2 : 1);
        reconnect_during_flush();
        flush_hook = invalidate_ready_during_flush;
        tick_control();
        assert(!flush_hook && !runtime.ui_gate);
        assert(!runtime.media.rtc_connected);
        assert(!runtime.usb_media || !runtime.usb_media->rtc_connected);
        tests++;
    }
}

static void test_ready_flush_failure_ends_session(void) {
    for (unsigned usb_failure = 0; usb_failure < 2; ++usb_failure) {
        start_ready_scene(usb_failure ? 2 : 1);
        reconnect_during_flush();
        fail_flush = !usb_failure;
        fail_usb_flush = usb_failure != 0;
        unsigned previous_flushes = flushes;
        tick_control();
        assert(flushes == previous_flushes + (usb_failure ? 2U : 1U));
        assert(runtime.media_cancel_requested && runtime.lifecycle.stop_request);
        assert(cleanup_requests == 1 && !runtime.media.rtc_connected);
        assert(!runtime.usb_media || !runtime.usb_media->rtc_connected);
        assert(!runtime.ui_gate);
        tests++;
    }
}

static void test_ready_preserves_pause_and_direction(void) {
    for (unsigned meeting = 0; meeting < 2; ++meeting) {
        start_ready_scene(meeting ? 2 : 1);
        runtime.input_direction = MYBOT_TRANSLATION_DIRECTION_SPEAK;
        runtime.media.paused = true;
        if (runtime.usb_media)
            runtime.usb_media->paused = true;
        reconnect_during_flush();
        tick_control();
        assert(runtime.media.rtc_connected && runtime.media.paused);
        assert(!runtime.usb_media ||
               (runtime.usb_media->rtc_connected && runtime.usb_media->paused));
        assert(runtime.input_direction == MYBOT_TRANSLATION_DIRECTION_SPEAK);
        assert(!pause_calls);
        tests++;
    }
}

static void test_ready_epoch_wrap(void) {
    start_ready_scene(1);
    runtime.rtc_media_state = INTPTR_MAX;
    registered.on_state_changed(MYBOT_RTC_STATE_RECONNECTING, registered.user_data);
    assert(runtime.rtc_media_state == 0 && !runtime.media.rtc_connected);
    registered.on_state_changed(MYBOT_RTC_STATE_CONNECTED, registered.user_data);
    assert(runtime.rtc_media_state == 3 && !runtime.media.rtc_connected);
    tick_control();
    assert(runtime.media.rtc_connected);
    tests++;
}

static void test_stopped_session_discards_pending_ready(void) {
    start_ready_scene(1);
    reconnect_during_flush();
    assert(runtime.rtc_media_state & 1);
    intptr_t previous_generation = runtime.conversation_generation;
    dev_on_conversation_stop(&runtime);
    assert(!runtime.rtc_media_state && runtime.conversation_generation != previous_generation);
    unsigned previous_flushes = flushes;
    tick_control();
    assert(flushes == previous_flushes && !runtime.media.rtc_connected);
    assert(runtime.media_cancel_requested);
    tests++;
}

static void test_all_scenes_quiesce_device_pcm_before_rtc(void) {
    for (unsigned scene = 0; scene < 3; ++scene) {
        if (scene == 2)
            start_meeting();
        else
            start_codec_translation(scene == 1);
        physical_device_pcm = 960;
        usb_device_pcm = scene == 2 ? 960 : 0;
        expected_stop_quiesces = scene == 2 ? 2 : 1;
        dev_on_conversation_stop(&runtime);
        assert(quiesces == expected_stop_quiesces && rtc_finalizations == 1 && ends == 1);
        assert(!physical_device_pcm && !usb_device_pcm);
        assert(!runtime.media.paused && !runtime.media_recovery_required);
        assert(!runtime.usb_media && !runtime.meeting_active);
        tests++;
    }
}

static void test_codec_quiesce_failure_retries_session_cleanup(void) {
    for (unsigned bidirectional = 0; bidirectional < 2; ++bidirectional) {
        for (unsigned retained = 0; retained < 2; ++retained) {
            start_codec_translation(bidirectional != 0);
            int16_t *retained_scratch = runtime.silent_audio;
            physical_device_pcm = 960;
            fail_physical_quiesce = true;
            fail_physical_end = retained != 0;
            expected_stop_quiesces = 1;
            dev_on_conversation_stop(&runtime);
            assert(ends == 1 && rtc_finalizations == 1);
            assert(runtime.media_recovery_required == (retained != 0));
            assert(runtime.media.paused == (retained != 0));
            assert(physical_device_pcm == (retained ? 960U : 0U));
            assert(runtime.silent_audio == retained_scratch);
            assert(runtime.media_cancel_requested && !runtime.media.rtc_connected);
            if (retained) {
                fail_physical_quiesce = fail_physical_end = false;
                expected_stop_quiesces = 2;
                dev_on_conversation_stop(&runtime);
                assert(!physical_device_pcm && !runtime.media_recovery_required);
                assert(!runtime.media.paused && ends == 2 && rtc_finalizations == 2);
            }
            tests++;
        }
    }
}

static void test_meeting_quiesce_failure_still_cleans_both_routes(void) {
    for (unsigned usb_failure = 0; usb_failure < 2; ++usb_failure) {
        for (unsigned retained = 0; retained < 2; ++retained) {
            start_meeting();
            mybot_media_pipeline_t *retained_usb = runtime.usb_media;
            physical_device_pcm = usb_device_pcm = 960;
            fail_physical_quiesce = !usb_failure;
            fail_usb_quiesce = usb_failure != 0;
            fail_physical_end = !usb_failure && retained;
            fail_usb_stop = usb_failure && retained;
            expected_stop_quiesces = 2;
            dev_on_conversation_stop(&runtime);
            assert(quiesces == 2 && rtc_finalizations == 1 && ends == 1);
            assert(runtime.media_recovery_required == (retained != 0));
            assert(!runtime.meeting_active);
            assert(physical_device_pcm == (!usb_failure && retained ? 960U : 0U));
            assert(usb_device_pcm == (usb_failure && retained ? 960U : 0U));
            assert(runtime.usb_media == (usb_failure && retained ? retained_usb : NULL));
            fail_physical_quiesce = fail_usb_quiesce = fail_physical_end = fail_usb_stop = false;
            if (retained) {
                /* A retained USB worker is no longer running, so only physical quiesces again. */
                expected_stop_quiesces = 3;
                dev_on_conversation_stop(&runtime);
                assert(!runtime.usb_media && !runtime.media_recovery_required && !allocations);
                assert(!physical_device_pcm && !usb_device_pcm);
            }
            tests++;
        }
    }
}

int main(void) {
    test_ready_waits_for_control_flush();
    test_ready_rejects_changed_rtc_epoch();
    test_ready_rechecks_cancellation_after_flush();
    test_ready_flush_failure_ends_session();
    test_ready_preserves_pause_and_direction();
    test_ready_epoch_wrap();
    test_stopped_session_discards_pending_ready();
    test_all_scenes_quiesce_device_pcm_before_rtc();
    test_codec_quiesce_failure_retries_session_cleanup();
    test_meeting_quiesce_failure_still_cleans_both_routes();
    test_meeting_routes_remain_independent();
    test_meeting_pause_failure_and_disconnect();
    test_meeting_teardown_retains_pending_workers();
    test_physical_audio_error_closes_session_media();
    test_initial_and_selected_uplink();
    test_media_single_route();
    test_switch_fences_and_filters_audio();
    test_switch_failure_and_stop();
    test_remote_gate_contention();
    test_start_failure_requests_parent_cleanup();
    test_uplink_failure_cancel_and_invalid_frame();
    reset();
    printf("translation face audio: %u route/fence/fault scenarios passed (cloud AEC=%d)\n", tests,
           MYBOT_CLOUD_AEC);
    return 0;
}
