/* SPDX-License-Identifier: Apache-2.0 */
#include <mybot/mybot.h>
#include <mybot/mybot_build_config.h>
#include <mybot/mybot_ui.h>
#include <mybot/platform/mybot_platform.h>

#include "mybot_agora_rtc.h"
#include "mybot_device_lifecycle.h"
#include "mybot_key_internal.h"
#include "mybot_kv_store_internal.h"
#include "mybot_media_pipeline.h"
#include "mybot_presenter.h"
#include "mybot_platform_registry.h"
#include "mybot_state_model.h"
#include "mybot_url.h"
#include "mybot_video_internal.h"
#include "mybot_wifi_internal.h"

#include <api/aosl.h>
#include <api/aosl_atomic.h>
#include <api/aosl_log.h>
#include <api/aosl_mpq.h>
#include <api/aosl_mpq_timer.h>
#include <api/aosl_time.h>
#include <hal/aosl_hal_memory.h>

#include <stdio.h>
#include <string.h>

#define STATE_TICK_MS 100
#define CONTROL_MPQ_STACK_SIZE 16384
#define SERVICE_MPQ_STACK_SIZE 16384
#define VOLUME_KEY_STEP 10
#define VOLUME_READ_RETRY_TICKS (1000 / STATE_TICK_MS)
#define UI_COMMAND_CAPACITY 8
typedef struct {
    aosl_atomic_t running;
    mybot_state_model_t state_model;
    aosl_atomic_t aosl_ref_held;
    mybot_config_t config;

    mybot_key_t key;
    mybot_kv_store_t kv_store;
    mybot_wifi_t wifi;
    mybot_presenter_t presenter;
    unsigned volume_retry_ticks;
    mybot_media_pipeline_t media;
    bool media_recovery_required;
#if MYBOT_ENABLE_VIDEO
    mybot_video_t video;
#endif
    aosl_atomic_t conversation_generation;
    aosl_atomic_t media_cancel_requested;
    mybot_device_lifecycle_t lifecycle;
    /* Snapshots used to reject stale RTM events from a previous conversation. */
    char rtc_channel[128];
    char rtc_agent_uid[MYBOT_RTM_UID_MAX_LEN];

    /* Sole owner of application state transitions and control resources. */
    aosl_mpq_t control_mpq;
    aosl_timer_t control_timer;
    aosl_mpq_t service_mpq;
    aosl_atomic_t service_results_enabled;
    aosl_atomic_t service_result_gate;
    mybot_device_service_request_t *completed_results;
    bool lifecycle_initialized;
    int control_start_result;
    /* Value-owned commands; notifications never own pointers into caller memory. */
    mybot_ui_command_t ui_commands[UI_COMMAND_CAPACITY];
    unsigned ui_head;
    unsigned ui_count;
    aosl_atomic_t ui_gate;
    aosl_atomic_t ui_accepting_commands;
    uintptr_t ui_generation;
} mybot_runtime_t;

static mybot_runtime_t s_default_runtime;
static aosl_atomic_t s_lifecycle_gate;
static aosl_atomic_t s_exit_generation;
static aosl_atomic_t s_run_exit_generation;
static uintptr_t s_ui_generation;

/* This gate must work before aosl_ctor(), so it intentionally uses only the
 * HAL-backed atomic primitive, matching AOSL's own lifecycle gate. */
static void lifecycle_lock(void) {
    while (aosl_atomic_cmpxchg(&s_lifecycle_gate, 0, 1) != 0) {
        aosl_msleep(5);
    }
}

static void lifecycle_unlock(void) {
    aosl_atomic_set(&s_lifecycle_gate, 0);
}

static mybot_state_t runtime_get_state(const mybot_runtime_t *runtime) {
    return mybot_state_model_get_view(&runtime->state_model).app_state;
}

static bool runtime_is_running(const mybot_runtime_t *runtime) {
    return aosl_atomic_read(&runtime->running) != 0 &&
           aosl_atomic_read(&s_exit_generation) == aosl_atomic_read(&s_run_exit_generation);
}

static void runtime_publish_exit(mybot_runtime_t *runtime) {
    aosl_atomic_inc(&s_exit_generation);
    aosl_atomic_set(&runtime->running, false);
}

static void control_start_conversation(mybot_runtime_t *runtime) {
    if (runtime_get_state(runtime) == MYBOT_STATE_READY ||
        runtime_get_state(runtime) == MYBOT_STATE_TRANSLATION_ERROR) {
        /* Finish any retained local queue before creating another cloud session. */
        if (mybot_agora_rtc_fini() < 0) {
            snprintf(runtime->lifecycle.error_code, sizeof(runtime->lifecycle.error_code),
                     "RTC_NOT_READY");
            mybot_presenter_set_translation(&runtime->presenter, runtime->lifecycle.source_language,
                                            runtime->lifecycle.target_language,
                                            runtime->lifecycle.error_code,
                                            runtime->presenter.volume);
            mybot_presenter_render_state(&runtime->presenter, &runtime->state_model);
            AOSL_LOG_ERR("local RTC cleanup incomplete; cloud translation start rejected");
            return;
        }
        if (runtime->media_recovery_required || mybot_media_pipeline_is_paused(&runtime->media)) {
            runtime->media_recovery_required =
                mybot_media_pipeline_end_session(&runtime->media) < 0;
            if (runtime->media_recovery_required) {
                snprintf(runtime->lifecycle.error_code, sizeof(runtime->lifecycle.error_code),
                         "MEDIA_NOT_READY");
                AOSL_LOG_ERR("local media recovery failed; cloud translation start rejected");
                return;
            }
            if (strcmp(runtime->lifecycle.error_code, "MEDIA_NOT_READY") == 0)
                runtime->lifecycle.error_code[0] = '\0';
        }
        aosl_atomic_set(&runtime->media_cancel_requested, false);
        mybot_device_lifecycle_request_start(&runtime->lifecycle);
    }
}

static void control_stop_conversation(mybot_runtime_t *runtime) {
    if (runtime_get_state(runtime) == MYBOT_STATE_IN_CONVERSATION ||
        runtime_get_state(runtime) == MYBOT_STATE_TRANSLATION_STARTING ||
        runtime_get_state(runtime) == MYBOT_STATE_CLEANUP_PENDING ||
        runtime_get_state(runtime) == MYBOT_STATE_TRANSLATION_STOPPING) {
        mybot_device_lifecycle_request_stop(&runtime->lifecycle);
    }
}

static void control_pair(mybot_runtime_t *runtime) {
    mybot_state_t state = runtime_get_state(runtime);
    if (state == MYBOT_STATE_READY || state == MYBOT_STATE_IN_CONVERSATION) {
        mybot_device_lifecycle_request_pair(&runtime->lifecycle);
    }
}

static void handle_start_conversation(const aosl_ts_t *queued_ts, aosl_refobj_t robj,
                                      uintptr_t argc, uintptr_t argv[]) {
    (void)queued_ts;
    (void)robj;
    if (argc == 1) {
        mybot_runtime_t *runtime = (mybot_runtime_t *)argv[0];
        if (runtime_is_running(runtime)) {
            control_start_conversation(runtime);
        }
    }
}

static void fail_control_queue(mybot_runtime_t *runtime, const char *event) {
    AOSL_LOG_ERR("failed to queue control event: %s", event);
    runtime_publish_exit(runtime);
}

static void lock_service_results(mybot_runtime_t *runtime) {
    while (aosl_atomic_cmpxchg(&runtime->service_result_gate, 0, 1) != 0)
        aosl_msleep(1);
}

/* Results own server side effects. A queued notification never owns the request. */
static void collect_service_result(mybot_runtime_t *runtime,
                                   mybot_device_service_request_t *request) {
    lock_service_results(runtime);
    request->retired_next = runtime->completed_results;
    runtime->completed_results = request;
    aosl_atomic_set(&runtime->service_result_gate, 0);
}

static void apply_completed_services(mybot_runtime_t *runtime) {
    lock_service_results(runtime);
    mybot_device_service_request_t *request = runtime->completed_results;
    runtime->completed_results = NULL;
    aosl_atomic_set(&runtime->service_result_gate, 0);
    while (request) {
        mybot_device_service_request_t *next = request->retired_next;
        if (runtime->lifecycle_initialized) {
            mybot_device_lifecycle_service_result(&runtime->lifecycle, request);
        }
        aosl_hal_free(request);
        request = next;
    }
}

static void handle_service_result(const aosl_ts_t *ts, aosl_refobj_t ref, uintptr_t argc,
                                  uintptr_t argv[]) {
    (void)ts;
    (void)ref;
    if (argc == 1)
        apply_completed_services((mybot_runtime_t *)argv[0]);
}

/* This worker only uses the request snapshot, never mutable lifecycle fields. */
static void execute_service_request(const aosl_ts_t *ts, aosl_refobj_t ref, uintptr_t argc,
                                    uintptr_t argv[]) {
    (void)ts;
    (void)ref;
    if (argc != 2) {
        return;
    }
    mybot_runtime_t *runtime = (mybot_runtime_t *)argv[0];
    mybot_device_service_request_t *request = (mybot_device_service_request_t *)argv[1];
    if (request->type != MYBOT_DEVICE_SERVICE_STOP &&
        !aosl_atomic_read(&runtime->service_results_enabled)) {
        if (request->type == MYBOT_DEVICE_SERVICE_START) {
            request->result = -1;
            memset(&request->payload.start.response, 0, sizeof(request->payload.start.response));
            snprintf(request->payload.start.response.error_code,
                     sizeof(request->payload.start.response.error_code), "START_CANCELLED");
            collect_service_result(runtime, request);
        } else {
            aosl_hal_free(request);
        }
        return;
    }

    if (request->type == MYBOT_DEVICE_SERVICE_START) {
        request->result = mybot_device_client_start_conversation(
            request->server_base, request->device_id, request->credential,
            request->payload.start.source_language, request->payload.start.target_language,
            &request->payload.start.response);
    } else if (request->type == MYBOT_DEVICE_SERVICE_STOP) {
        request->result = mybot_device_client_stop_conversation(
            request->server_base, request->device_id, request->credential,
            request->payload.stop.conversation_id, request->payload.stop.reason);
    } else {
        char auth[MYBOT_DEVICE_CLIENT_MAX_TOKEN + 16];
        int written = snprintf(auth, sizeof(auth), "%s %s",
                               request->type == MYBOT_DEVICE_SERVICE_PAIR_POLL ? "Pair" : "Device",
                               request->credential);
        request->result =
            written < 0 || (size_t)written >= sizeof(auth)
                ? -1
                : mybot_device_client_get_binding_status(request->server_base, request->device_id,
                                                         auth, &request->payload.binding);
    }

    collect_service_result(runtime, request);
    if (aosl_atomic_read(&runtime->service_results_enabled) &&
        aosl_mpq_queue(runtime->control_mpq, AOSL_MPQ_INVALID, AOSL_REF_INVALID,
                       "handle_service_result", handle_service_result, 1, (uintptr_t)runtime) < 0) {
        fail_control_queue(runtime, "service result");
    }
}

static int submit_service_request(mybot_device_service_request_t *request, void *user_data) {
    mybot_runtime_t *runtime = user_data;
    if (aosl_mpq_invalid(runtime->service_mpq) ||
        !aosl_atomic_read(&runtime->service_results_enabled)) {
        return -1;
    }
    return aosl_mpq_queue(runtime->service_mpq, AOSL_MPQ_INVALID, AOSL_REF_INVALID,
                          "execute_service_request", execute_service_request, 2, (uintptr_t)runtime,
                          (uintptr_t)request);
}

static bool destroy_service_queue(mybot_runtime_t *runtime) {
    aosl_atomic_set(&runtime->service_results_enabled, false);
    if (aosl_mpq_invalid(runtime->service_mpq)) {
        return true;
    }
    if (aosl_mpq_destroy_wait(runtime->service_mpq) < 0) {
        AOSL_LOG_ERR("failed to destroy background service queue");
        return false;
    }
    runtime->service_mpq = AOSL_MPQ_INVALID;
    return true;
}

static void sync_wake_words(mybot_runtime_t *runtime) {
#if MYBOT_WAKE_WORDS
    mybot_state_view_t state = mybot_state_model_get_view(&runtime->state_model);
    bool enabled =
        state.app_state == MYBOT_STATE_READY && state.device_state == MYBOT_DEVICE_STATE_RUNTIME;
    mybot_media_pipeline_set_wake_words_enabled(&runtime->media, enabled);
#else
    (void)runtime;
#endif
}

static int media_send_audio(const void *data, size_t len, void *user_data) {
    mybot_runtime_t *runtime = user_data;
    if (aosl_atomic_read(&runtime->media_cancel_requested))
        return 0;
    return mybot_agora_rtc_send_audio(data, len);
}

static void media_on_wake_word(const char *wake_word, void *user_data) {
    mybot_runtime_t *runtime = user_data;
    AOSL_LOG_NTC("[WAKE WORDS] detected: %s", wake_word ? wake_word : "<unspecified>");
    if (!runtime_is_running(runtime) || runtime_get_state(runtime) != MYBOT_STATE_READY) {
        return;
    }
    if (aosl_mpq_queue(runtime->control_mpq, AOSL_MPQ_INVALID, AOSL_REF_INVALID,
                       "handle_start_conversation", handle_start_conversation, 1,
                       (uintptr_t)runtime) < 0) {
        fail_control_queue(runtime, "wake-word conversation start");
    }
}

#if MYBOT_ENABLE_VIDEO
typedef enum {
    VIDEO_CONTROL_START,
    VIDEO_CONTROL_KEY_FRAME,
    VIDEO_CONTROL_BITRATE,
} video_control_event_t;

static void handle_video_control(const aosl_ts_t *ts, aosl_refobj_t ref, uintptr_t argc,
                                 uintptr_t argv[]) {
    (void)ts;
    (void)ref;
    if (argc != 4) {
        return;
    }
    mybot_runtime_t *runtime = (mybot_runtime_t *)argv[0];
    if (!runtime_is_running(runtime) ||
        argv[1] != (uintptr_t)aosl_atomic_read(&runtime->conversation_generation) ||
        runtime_get_state(runtime) != MYBOT_STATE_IN_CONVERSATION || !runtime->rtc_channel[0] ||
        aosl_atomic_read(&runtime->lifecycle.stop_request) ||
        !aosl_atomic_read(&runtime->media.rtc_connected)) {
        return;
    }
    switch ((video_control_event_t)argv[2]) {
    case VIDEO_CONTROL_START:
        if (mybot_video_start(&runtime->video) < 0) {
            AOSL_LOG_ERR("failed to start video source");
        }
        break;
    case VIDEO_CONTROL_KEY_FRAME:
        mybot_video_request_key_frame(&runtime->video);
        break;
    case VIDEO_CONTROL_BITRATE:
        mybot_video_set_target_bitrate(&runtime->video, (uint32_t)argv[3]);
        break;
    }
}

static void queue_video_control(mybot_runtime_t *runtime, video_control_event_t event,
                                uint32_t target_bps) {
    if (!runtime_is_running(runtime)) {
        return;
    }
    /* Copy scalars into the queue; only the control worker touches the encoder. */
    uintptr_t generation = (uintptr_t)aosl_atomic_read(&runtime->conversation_generation);
    if (aosl_mpq_queue(runtime->control_mpq, AOSL_MPQ_INVALID, AOSL_REF_INVALID,
                       "handle_video_control", handle_video_control, 4, (uintptr_t)runtime,
                       generation, (uintptr_t)event, (uintptr_t)target_bps) < 0) {
        if (event == VIDEO_CONTROL_START) {
            fail_control_queue(runtime, "video start");
        } else {
            AOSL_LOG_WRN("failed to queue video control event (%d)", (int)event);
        }
    }
}
#endif

static void rtc_on_remote_audio(uint32_t uid, const void *data, size_t len, void *user_data) {
    (void)uid;
    mybot_runtime_t *runtime = user_data;
    if (!aosl_atomic_read(&runtime->media_cancel_requested)) {
        mybot_media_pipeline_push_remote_audio(&runtime->media, data, len);
    }
}

static void rtc_on_state_changed(mybot_rtc_state_t state, void *user_data) {
    mybot_runtime_t *runtime = user_data;
    bool connected =
        state == MYBOT_RTC_STATE_CONNECTED && !aosl_atomic_read(&runtime->media_cancel_requested);
    mybot_media_pipeline_set_rtc_connected(&runtime->media, connected);
#if MYBOT_ENABLE_VIDEO
    if (connected) {
        queue_video_control(runtime, VIDEO_CONTROL_START, 0);
    }
#endif
    AOSL_LOG_NTC("rtc -> %s", connected ? "connected" : "disconnected");

    if (state == MYBOT_RTC_STATE_DISCONNECTED || state == MYBOT_RTC_STATE_ERROR) {
        mybot_device_lifecycle_notify_conversation_ended(&runtime->lifecycle);
    }
}

static void rtc_on_rtm_event(const char *rtm_uid, mybot_rtm_event_type_t event_type, int error_code,
                             void *user_data) {
    mybot_runtime_t *runtime = user_data;
    AOSL_LOG_DBG("[RTM] application event: uid=%s type=%d error=%d", rtm_uid ? rtm_uid : "(null)",
                 (int)event_type, error_code);
    if ((event_type == MYBOT_RTM_EVENT_LOGIN && error_code != 0) ||
        event_type == MYBOT_RTM_EVENT_KICKOFF || event_type == MYBOT_RTM_EVENT_EXIT) {
        mybot_device_lifecycle_notify_conversation_ended(&runtime->lifecycle);
    }
}

static void rtc_on_rtm_subscribe_result(const char *channel, int error_code, void *user_data) {
    mybot_runtime_t *runtime = user_data;
    if (!runtime_is_running(runtime)) {
        AOSL_LOG_DBG("[RTM] application subscribe result ignored: runtime is not running");
        return;
    }
    AOSL_LOG_DBG("[RTM] application subscribe result: channel=%s error=%d",
                 channel ? channel : "(null)", error_code);
    if (error_code != 0) {
        mybot_device_lifecycle_notify_conversation_ended(&runtime->lifecycle);
    }
}

static void rtc_on_token_will_expire(void *user_data) {
    mybot_runtime_t *runtime = user_data;
    if (runtime_is_running(runtime)) {
        mybot_device_lifecycle_notify_credentials_expiring(&runtime->lifecycle);
    }
}

#if MYBOT_ENABLE_VIDEO
static void rtc_on_video_key_frame_requested(void *user_data) {
    mybot_runtime_t *runtime = user_data;
    queue_video_control(runtime, VIDEO_CONTROL_KEY_FRAME, 0);
}

static void rtc_on_video_target_bitrate_changed(uint32_t target_bps, void *user_data) {
    mybot_runtime_t *runtime = user_data;
    queue_video_control(runtime, VIDEO_CONTROL_BITRATE, target_bps);
}
#endif

static void dev_on_pair_code(const char *code, void *user_data) {
    mybot_runtime_t *runtime = user_data;
    if (!runtime_is_running(runtime)) {
        return;
    }
    mybot_state_t state = runtime_get_state(runtime);
    if (state == MYBOT_STATE_STOPPING || state == MYBOT_STATE_FAILED ||
        state == MYBOT_STATE_WIFI_DISCONNECTED) {
        return;
    }

    AOSL_LOG_NTC("pair code: %s", code);
    mybot_presenter_show_pair_code(&runtime->presenter, code);
    (void)mybot_media_pipeline_play_prompt(&runtime->media, MYBOT_PROMPT_PAIR_CODE, code);
}

static void dev_on_conversation_start(const mybot_device_conversation_t *params, void *user_data) {
    mybot_runtime_t *runtime = user_data;
    if (!runtime_is_running(runtime) || aosl_atomic_read(&runtime->media_cancel_requested)) {
        mybot_device_lifecycle_notify_conversation_ended(&runtime->lifecycle);
        return;
    }
    if (mybot_media_pipeline_flush_session(&runtime->media) < 0) {
        AOSL_LOG_ERR("failed to flush media session before RTC join");
        mybot_device_lifecycle_notify_conversation_ended(&runtime->lifecycle);
        return;
    }

    mybot_agora_rtc_callbacks_t callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    if (!params || params->rtc_agent_uid[0] == '\0') {
        AOSL_LOG_ERR("conversation response missing RTM agent UID");
        mybot_device_lifecycle_notify_conversation_ended(&runtime->lifecycle);
        return;
    }
    snprintf(runtime->rtc_channel, sizeof(runtime->rtc_channel), "%s", params->rtc_channel);
    snprintf(runtime->rtc_agent_uid, sizeof(runtime->rtc_agent_uid), "%s", params->rtc_agent_uid);
    aosl_atomic_inc(&runtime->conversation_generation);
    callbacks.on_remote_audio = rtc_on_remote_audio;
    callbacks.on_state_changed = rtc_on_state_changed;
    callbacks.on_token_will_expire = rtc_on_token_will_expire;
    callbacks.on_rtm_event = rtc_on_rtm_event;
    callbacks.on_rtm_subscribe_result = rtc_on_rtm_subscribe_result;
#if MYBOT_ENABLE_VIDEO
    callbacks.on_video_key_frame_requested = rtc_on_video_key_frame_requested;
    callbacks.on_video_target_bitrate_changed = rtc_on_video_target_bitrate_changed;
#endif
    callbacks.user_data = runtime;

    if (mybot_agora_rtc_init(params->rtc_app_id, &callbacks) < 0) {
        AOSL_LOG_ERR("failed to initialize Agora RTC");
        mybot_device_lifecycle_notify_conversation_ended(&runtime->lifecycle);
        return;
    }

    AOSL_LOG_NTC("joining RTC channel=%s uid=%s", params->rtc_channel, params->rtc_uid);
    if (mybot_agora_rtc_join(params->rtc_channel, params->rtc_token, params->rtc_uid,
                             params->rtc_agent_uid, params->audio_publisher_uid) < 0) {
        AOSL_LOG_ERR("failed to join Agora RTC channel");
        mybot_device_lifecycle_notify_conversation_ended(&runtime->lifecycle);
        return;
    }
    AOSL_LOG_NTC("RTC join requested");
}

static void dev_on_conversation_stop(void *user_data) {
    mybot_runtime_t *runtime = user_data;
    aosl_atomic_set(&runtime->media_cancel_requested, true);
    aosl_atomic_inc(&runtime->conversation_generation);
    runtime->rtc_channel[0] = '\0';
    runtime->rtc_agent_uid[0] = '\0';
#if MYBOT_ENABLE_VIDEO
    if (mybot_video_stop(&runtime->video) < 0) {
        AOSL_LOG_ERR("failed to stop video source");
    }
#endif
    /* Render from the current state snapshot before the lifecycle publishes its
     * next device state, so the old conversation overlay cannot win a race. */
    mybot_presenter_render_state(&runtime->presenter, &runtime->state_model);
    /* RTSA belongs to this conversation, including its App ID and callback queue. */
    if (mybot_agora_rtc_fini() < 0) {
        const char *error = runtime->lifecycle.error_code;
        if (!error[0] || strcmp(error, "START_UNKNOWN") == 0 ||
            strcmp(error, "CLEANUP_PENDING") == 0 || strcmp(error, "CLEANUP_AUTH_REJECTED") == 0)
            snprintf(runtime->lifecycle.error_code, sizeof(runtime->lifecycle.error_code),
                     "RTC_NOT_READY");
        AOSL_LOG_ERR("failed to finalize RTC conversation");
    }
    runtime->media_recovery_required = mybot_media_pipeline_end_session(&runtime->media) < 0;
    if (runtime->media_recovery_required)
        AOSL_LOG_ERR("local media teardown incomplete; the next start must recover audio first");
}

static void refresh_translation_view(mybot_runtime_t *runtime) {
    int volume = -1;
    (void)mybot_media_pipeline_get_volume(&runtime->media, &volume);
    runtime->volume_retry_ticks = 0;
    mybot_presenter_set_translation(&runtime->presenter, runtime->lifecycle.source_language,
                                    runtime->lifecycle.target_language,
                                    runtime->lifecycle.error_code, volume);
    runtime->presenter.media_ready = aosl_atomic_read(&runtime->media.rtc_connected) != 0;
    runtime->presenter.paused = mybot_media_pipeline_is_paused(&runtime->media);
}

static bool ui_command_valid(const mybot_ui_command_t *command) {
    if (!command)
        return false;
    switch (command->type) {
    case MYBOT_UI_SET_VOLUME:
        return command->value >= 0 && command->value <= 100;
    case MYBOT_UI_SET_PAUSED:
        return command->value == 0 || command->value == 1;
    case MYBOT_UI_SET_LANGUAGES: {
        if (!memchr(command->source_language, '\0', sizeof(command->source_language)) ||
            !memchr(command->target_language, '\0', sizeof(command->target_language)) ||
            strcmp(command->source_language, command->target_language) == 0)
            return false;
        bool source_supported = false, target_supported = false;
        size_t count = 0;
        const mybot_ui_language_t *languages = mybot_ui_languages(&count);
        for (size_t i = 0; i < count; ++i) {
            source_supported |= strcmp(command->source_language, languages[i].code) == 0;
            target_supported |= strcmp(command->target_language, languages[i].code) == 0;
        }
        return source_supported && target_supported;
    }
    default:
        return false;
    }
}

static void lock_ui_commands(mybot_runtime_t *runtime) {
    while (aosl_atomic_cmpxchg(&runtime->ui_gate, 0, 1) != 0)
        aosl_msleep(1);
}

static void close_ui_commands(mybot_runtime_t *runtime) {
    aosl_atomic_set(&runtime->ui_accepting_commands, false);
    lock_ui_commands(runtime);
    runtime->ui_head = runtime->ui_count = 0;
    aosl_atomic_set(&runtime->ui_gate, 0);
}

static void handle_ui_command(const aosl_ts_t *ts, aosl_refobj_t ref, uintptr_t argc,
                              uintptr_t argv[]) {
    (void)ts;
    (void)ref;
    if (argc != 2)
        return;
    mybot_runtime_t *runtime = (mybot_runtime_t *)argv[0];
    if (argv[1] != runtime->ui_generation)
        return;
    lock_ui_commands(runtime);
    if (runtime->ui_count == 0) {
        aosl_atomic_set(&runtime->ui_gate, 0);
        return;
    }
    mybot_ui_command_t command = runtime->ui_commands[runtime->ui_head];
    runtime->ui_head = (runtime->ui_head + 1) % UI_COMMAND_CAPACITY;
    runtime->ui_count--;
    aosl_atomic_set(&runtime->ui_gate, 0);
    if (!runtime_is_running(runtime) || !aosl_atomic_read(&runtime->ui_accepting_commands))
        return;

    int result = -1;
    switch (command.type) {
    case MYBOT_UI_SET_LANGUAGES:
        result = mybot_device_lifecycle_set_languages(&runtime->lifecycle, command.source_language,
                                                      command.target_language);
        break;
    case MYBOT_UI_SET_VOLUME:
        result = mybot_media_pipeline_set_volume(&runtime->media, command.value);
        break;
    case MYBOT_UI_SET_PAUSED:
        if (runtime_get_state(runtime) == MYBOT_STATE_IN_CONVERSATION &&
            aosl_atomic_read(&runtime->media.rtc_connected) &&
            !aosl_atomic_read(&runtime->media_cancel_requested) &&
            !aosl_atomic_read(&runtime->lifecycle.stop_request))
            result = mybot_media_pipeline_set_paused(&runtime->media, command.value != 0);
        break;
    }
    if (result < 0)
        AOSL_LOG_WRN("event=ui_command type=%d result=rejected", (int)command.type);
    refresh_translation_view(runtime);
    mybot_presenter_render_state(&runtime->presenter, &runtime->state_model);
}

int mybot_ui_submit(const mybot_ui_command_t *command) {
    if (!ui_command_valid(command))
        return -1;
    /* Queue lifetime is protected without waiting on start/stop or HTTP teardown. */
    if (aosl_atomic_cmpxchg(&s_lifecycle_gate, 0, 1) != 0)
        return -1;
    mybot_runtime_t *runtime = &s_default_runtime;
    if (!runtime_is_running(runtime) || !aosl_atomic_read(&runtime->ui_accepting_commands) ||
        aosl_mpq_invalid(runtime->control_mpq) ||
        aosl_atomic_cmpxchg(&runtime->ui_gate, 0, 1) != 0) {
        lifecycle_unlock();
        return -1;
    }
    if (runtime->ui_count == UI_COMMAND_CAPACITY) {
        aosl_atomic_set(&runtime->ui_gate, 0);
        lifecycle_unlock();
        return -1;
    }
    unsigned tail = (runtime->ui_head + runtime->ui_count) % UI_COMMAND_CAPACITY;
    runtime->ui_commands[tail] = *command;
    runtime->ui_count++;
    int result = aosl_mpq_queue(runtime->control_mpq, AOSL_MPQ_INVALID, AOSL_REF_INVALID,
                                "handle_ui_command", handle_ui_command, 2, (uintptr_t)runtime,
                                runtime->ui_generation);
    if (result < 0)
        runtime->ui_count--; /* Consumer cannot remove a command until this gate is released. */
    aosl_atomic_set(&runtime->ui_gate, 0);
    lifecycle_unlock();
    return result < 0 ? -1 : 0;
}

static void dev_on_configuration_changed(const char *source, const char *target, void *user_data) {
    (void)source;
    (void)target;
    mybot_runtime_t *runtime = user_data;
    refresh_translation_view(runtime);
    mybot_presenter_render_state(&runtime->presenter, &runtime->state_model);
}

static void dev_on_state_changed(mybot_device_state_t state, void *user_data) {
    mybot_runtime_t *runtime = user_data;
    if (!runtime_is_running(runtime)) {
        return;
    }
    (void)mybot_state_model_set_device_state(&runtime->state_model, state);
    refresh_translation_view(runtime);

    if (state != MYBOT_DEVICE_STATE_AWAITING_CLAIM) {
        mybot_media_pipeline_stop_prompt(&runtime->media);
    }
    sync_wake_words(runtime);
    mybot_presenter_render_state(&runtime->presenter, &runtime->state_model);
}

static void handle_key_event(const aosl_ts_t *queued_ts, aosl_refobj_t robj, uintptr_t argc,
                             uintptr_t argv[]) {
    (void)queued_ts;
    (void)robj;
    if (argc != 2) {
        return;
    }

    mybot_runtime_t *runtime = (mybot_runtime_t *)argv[0];
    if (!runtime_is_running(runtime)) {
        return;
    }
    mybot_key_event_t event = (mybot_key_event_t)argv[1];
    switch (event) {
    case MYBOT_KEY_EVENT_CONVERSATION_START:
        control_start_conversation(runtime);
        break;
    case MYBOT_KEY_EVENT_CONVERSATION_STOP:
        control_stop_conversation(runtime);
        break;
    case MYBOT_KEY_EVENT_PAIR:
        control_pair(runtime);
        break;
    case MYBOT_KEY_EVENT_VOLUME_UP:
        mybot_media_pipeline_adjust_volume(&runtime->media, VOLUME_KEY_STEP);
        break;
    case MYBOT_KEY_EVENT_VOLUME_DOWN:
        mybot_media_pipeline_adjust_volume(&runtime->media, -VOLUME_KEY_STEP);
        break;
    case MYBOT_KEY_EVENT_SOURCE_LANGUAGE_NEXT:
        (void)mybot_device_lifecycle_cycle_language(&runtime->lifecycle, true);
        break;
    case MYBOT_KEY_EVENT_TARGET_LANGUAGE_NEXT:
        (void)mybot_device_lifecycle_cycle_language(&runtime->lifecycle, false);
        break;
    case MYBOT_KEY_EVENT_EXIT:
        break;
    }
    refresh_translation_view(runtime);
    mybot_presenter_render_state(&runtime->presenter, &runtime->state_model);
}

static void on_key_event(mybot_key_event_t event, void *user_data) {
    mybot_runtime_t *runtime = user_data;
    if (event == MYBOT_KEY_EVENT_CONVERSATION_STOP) {
        aosl_atomic_set(&runtime->media_cancel_requested, true);
    }
    if (event == MYBOT_KEY_EVENT_EXIT) {
        runtime_publish_exit(runtime);
        return;
    }
    if (!runtime_is_running(runtime)) {
        return;
    }

    if (aosl_mpq_queue(runtime->control_mpq, AOSL_MPQ_INVALID, AOSL_REF_INVALID, "handle_key_event",
                       handle_key_event, 2, (uintptr_t)runtime, (uintptr_t)event) < 0) {
        fail_control_queue(runtime, "key");
    }
}

static void control_tick_timer(aosl_timer_t id, const aosl_ts_t *now, uintptr_t argc,
                               uintptr_t argv[]) {
    (void)id;
    (void)now;
    if (argc != 1) {
        return;
    }

    mybot_runtime_t *runtime = (mybot_runtime_t *)argv[0];
    if (runtime_is_running(runtime)) {
        mybot_device_lifecycle_tick(&runtime->lifecycle);
        bool ready = aosl_atomic_read(&runtime->media.rtc_connected) != 0;
        bool media_changed = ready != runtime->presenter.media_ready;
        /* Recover an unavailable read without polling healthy hardware volume controls. */
        bool retry_volume = runtime->presenter.volume < 0 &&
                            ++runtime->volume_retry_ticks >= VOLUME_READ_RETRY_TICKS;
        if (media_changed || retry_volume) {
            refresh_translation_view(runtime);
            if (media_changed || runtime->presenter.volume >= 0) {
                mybot_presenter_render_state(&runtime->presenter, &runtime->state_model);
            }
        }
    }
}

static void cleanup_services(mybot_runtime_t *runtime) {
    close_ui_commands(runtime);
    aosl_atomic_set(&runtime->service_results_enabled, false);
    mybot_key_deinit(&runtime->key);

    if (!aosl_mpq_timer_invalid(runtime->control_timer)) {
        if (aosl_mpq_kill_timer(runtime->control_timer) < 0) {
            AOSL_LOG_ERR("failed to stop device lifecycle timer");
        }
        runtime->control_timer = AOSL_MPQ_TIMER_INVALID;
    }

    if (mybot_media_pipeline_stop(&runtime->media) < 0) {
        AOSL_LOG_ERR("media pipeline stop incomplete");
    }
#if MYBOT_ENABLE_VIDEO
    if (mybot_video_stop(&runtime->video) < 0) {
        AOSL_LOG_ERR("video source stop incomplete");
    }
#endif
    /* Stop audio I/O before the lifecycle callback performs network/RTC
     * shutdown. This guarantees a blocked capture or playback operation is
     * unblocked before the control worker waits on conversation teardown. */
    if (runtime->lifecycle_initialized) {
        mybot_device_lifecycle_shutdown(&runtime->lifecycle);
    }
    if (!destroy_service_queue(runtime)) {
        AOSL_LOG_ERR("HTTP teardown pending; retaining lifecycle storage");
        return;
    }
    apply_completed_services(runtime);
    if (runtime->lifecycle_initialized) {
        mybot_device_lifecycle_finish_shutdown(&runtime->lifecycle);
        runtime->lifecycle_initialized = false;
    }
    mybot_kv_store_deinit(&runtime->kv_store);
}

static int start_services(mybot_runtime_t *runtime) {
    if (mybot_kv_store_init(&runtime->kv_store) < 0 ||
        mybot_key_init(&runtime->key, on_key_event, runtime) < 0) {
        goto fail;
    }

    mybot_media_pipeline_callbacks_t media_cbs;
    memset(&media_cbs, 0, sizeof(media_cbs));
    media_cbs.send_audio = media_send_audio;
    media_cbs.on_wake_word = media_on_wake_word;
    media_cbs.user_data = runtime;
    mybot_media_pipeline_init(&runtime->media);
    if (mybot_media_pipeline_start(&runtime->media, &media_cbs) < 0) {
        goto fail;
    }
    runtime->media_recovery_required = false;

#if MYBOT_ENABLE_VIDEO
    mybot_video_init(&runtime->video);
    if (!runtime->video.initialized) {
        goto fail;
    }
#endif

    runtime->service_mpq = aosl_mpq_create_flags(AOSL_MPQ_FLAG_SIGP_EVENT | AOSL_MPQ_FLAG_NONBLOCK,
                                                 AOSL_THRD_PRI_NORMAL, SERVICE_MPQ_STACK_SIZE, 1000,
                                                 "service_mpq", NULL, NULL, NULL);
    if (aosl_mpq_invalid(runtime->service_mpq)) {
        AOSL_LOG_ERR("failed to create background service queue");
        goto fail;
    }
    aosl_atomic_set(&runtime->service_results_enabled, true);

    mybot_device_lifecycle_callbacks_t lifecycle_cbs;
    memset(&lifecycle_cbs, 0, sizeof(lifecycle_cbs));
    lifecycle_cbs.on_pair_code = dev_on_pair_code;
    lifecycle_cbs.on_conversation_start = dev_on_conversation_start;
    lifecycle_cbs.on_conversation_stop = dev_on_conversation_stop;
    lifecycle_cbs.on_state_changed = dev_on_state_changed;
    lifecycle_cbs.on_configuration_changed = dev_on_configuration_changed;
    lifecycle_cbs.submit_service_request = submit_service_request;
    lifecycle_cbs.user_data = runtime;
    if (mybot_device_lifecycle_init(
            &runtime->lifecycle, &runtime->kv_store, runtime->config.server_base,
            runtime->config.device_id, runtime->config.firmware_ver, runtime->config.hw_model,
            runtime->config.source_language, runtime->config.target_language, &lifecycle_cbs) < 0) {
        goto fail;
    }
    runtime->lifecycle_initialized = true;

    runtime->control_timer =
        aosl_mpq_set_timer(STATE_TICK_MS, control_tick_timer, NULL, 1, (uintptr_t)runtime);
    if (aosl_mpq_timer_invalid(runtime->control_timer)) {
        AOSL_LOG_ERR("failed to create device lifecycle timer");
        goto fail;
    }
    aosl_atomic_set(&runtime->ui_accepting_commands, true);
    return 0;

fail:
    cleanup_services(runtime);
#if MYBOT_ENABLE_VIDEO
    if (mybot_video_destroy(&runtime->video) < 0) {
        AOSL_LOG_ERR("video source destroy incomplete");
    }
#endif
    if (mybot_media_pipeline_destroy(&runtime->media) < 0) {
        AOSL_LOG_ERR("media pipeline destroy skipped because stop was incomplete");
    }
    return -1;
}

static void handle_wifi_event(const aosl_ts_t *queued_ts, aosl_refobj_t robj, uintptr_t argc,
                              uintptr_t argv[]) {
    (void)queued_ts;
    (void)robj;
    if (argc != 3) {
        return;
    }

    mybot_runtime_t *runtime = (mybot_runtime_t *)argv[0];
    if (!runtime_is_running(runtime)) {
        return;
    }
    mybot_wifi_event_t event = (mybot_wifi_event_t)argv[1];
    bool network_published = argv[2] != 0;
    mybot_state_t state = runtime_get_state(runtime);
    if (state == MYBOT_STATE_STOPPING || state == MYBOT_STATE_FAILED) {
        return;
    }

    if (runtime->lifecycle_initialized && !network_published) {
        if (event == MYBOT_WIFI_EVENT_STA_CONNECTED) {
            mybot_device_lifecycle_set_network_available(&runtime->lifecycle, true);
        } else if (event == MYBOT_WIFI_EVENT_STA_DISCONNECTED || event == MYBOT_WIFI_EVENT_FAILED) {
            mybot_device_lifecycle_set_network_available(&runtime->lifecycle, false);
        }
    }

    if (event == MYBOT_WIFI_EVENT_FAILED && state == MYBOT_STATE_WIFI_PROVISIONING) {
        if (mybot_state_model_fail(&runtime->state_model)) {
            mybot_presenter_show_screen(&runtime->presenter, MYBOT_LCD_SCREEN_FAILED);
            runtime_publish_exit(runtime);
        }
        return;
    }

    if (event == MYBOT_WIFI_EVENT_STA_DISCONNECTED ||
        (event == MYBOT_WIFI_EVENT_FAILED && state != MYBOT_STATE_WIFI_PROVISIONING)) {
        if (state != MYBOT_STATE_WIFI_DISCONNECTED &&
            mybot_state_model_network_lost(&runtime->state_model)) {
            sync_wake_words(runtime);
        }
        if (runtime_get_state(runtime) == MYBOT_STATE_WIFI_DISCONNECTED) {
            mybot_presenter_show_screen(&runtime->presenter, MYBOT_LCD_SCREEN_WIFI_DISCONNECTED);
        }
        return;
    }

    if (event == MYBOT_WIFI_EVENT_STA_CONNECTED && state == MYBOT_STATE_WIFI_DISCONNECTED) {
        if (mybot_state_model_network_restored(&runtime->state_model)) {
            sync_wake_words(runtime);
            mybot_presenter_render_state(&runtime->presenter, &runtime->state_model);
        }
        return;
    }

    if (event != MYBOT_WIFI_EVENT_STA_CONNECTED ||
        !mybot_state_model_begin_services(&runtime->state_model)) {
        return;
    }

    mybot_presenter_show_screen(&runtime->presenter, MYBOT_LCD_SCREEN_STARTING_SERVICES);
    if (start_services(runtime) < 0) {
        if (mybot_state_model_fail(&runtime->state_model)) {
            mybot_presenter_show_screen(&runtime->presenter, MYBOT_LCD_SCREEN_FAILED);
            runtime_publish_exit(runtime);
        }
        return;
    }

    if (mybot_state_model_services_ready(&runtime->state_model)) {
        AOSL_LOG_NTC("application services ready");
        sync_wake_words(runtime);
        mybot_presenter_render_state(&runtime->presenter, &runtime->state_model);
    }
}

static void on_wifi_event(mybot_wifi_event_t event, void *user_data) {
    mybot_runtime_t *runtime = user_data;
    if (!runtime_is_running(runtime)) {
        return;
    }
    mybot_state_t state = runtime_get_state(runtime);
    if (state == MYBOT_STATE_STOPPING || state == MYBOT_STATE_FAILED ||
        state == MYBOT_STATE_STOPPED) {
        return;
    }

    bool network_published = false;
    if (runtime->lifecycle_initialized) {
        if (event == MYBOT_WIFI_EVENT_STA_CONNECTED) {
            mybot_device_lifecycle_set_network_available(&runtime->lifecycle, true);
            network_published = true;
        } else if (event == MYBOT_WIFI_EVENT_STA_DISCONNECTED || event == MYBOT_WIFI_EVENT_FAILED) {
            mybot_device_lifecycle_set_network_available(&runtime->lifecycle, false);
            network_published = true;
        }
    }

    if (aosl_mpq_queue(runtime->control_mpq, AOSL_MPQ_INVALID, AOSL_REF_INVALID,
                       "handle_wifi_event", handle_wifi_event, 3, (uintptr_t)runtime,
                       (uintptr_t)event, (uintptr_t)network_published) < 0) {
        fail_control_queue(runtime, "Wi-Fi");
    }
}

static bool server_url_is_valid(const char *url) {
    mybot_url_parts_t parts;
    return mybot_url_parse(url, &parts) == 0;
}

static bool config_is_valid(const mybot_config_t *cfg) {
    return cfg && memchr(cfg->server_base, '\0', sizeof(cfg->server_base)) &&
           memchr(cfg->device_id, '\0', sizeof(cfg->device_id)) &&
           memchr(cfg->firmware_ver, '\0', sizeof(cfg->firmware_ver)) &&
           memchr(cfg->hw_model, '\0', sizeof(cfg->hw_model)) &&
           memchr(cfg->source_language, '\0', sizeof(cfg->source_language)) &&
           memchr(cfg->target_language, '\0', sizeof(cfg->target_language)) &&
           cfg->source_language[0] && cfg->target_language[0] &&
           strcmp(cfg->source_language, cfg->target_language) != 0 && cfg->server_base[0] &&
           cfg->device_id[0] && server_url_is_valid(cfg->server_base);
}

static bool platform_requirements_are_met(const mybot_config_t *cfg) {
    if (!mybot_platform_registry_is_registered()) {
        AOSL_LOG_ERR("platform descriptor is not registered");
        return false;
    }
#if MYBOT_WAKE_WORDS
    if (!mybot_platform_registry_get()->wake_words) {
        AOSL_LOG_ERR("wake-word platform operations are required but unavailable");
        return false;
    }
#endif
#if MYBOT_ENABLE_VIDEO
    if (!mybot_platform_registry_get()->video) {
        AOSL_LOG_ERR("video platform operations are required but unavailable");
        return false;
    }
#endif
    if (strncmp(cfg->server_base, "https://", 8) == 0 && !mybot_platform_registry_get()->https) {
        AOSL_LOG_ERR("HTTPS platform operations are required but unavailable");
        return false;
    }
    return true;
}

static void control_stop_runtime(mybot_runtime_t *runtime) {
    if (runtime_get_state(runtime) == MYBOT_STATE_STOPPED) {
        return;
    }

    mybot_state_t previous = runtime_get_state(runtime);
    AOSL_LOG_NTC("application control stopping");
    mybot_state_model_begin_stop(&runtime->state_model);
    runtime_publish_exit(runtime);
    if (previous != MYBOT_STATE_FAILED) {
        mybot_presenter_show_screen(&runtime->presenter, MYBOT_LCD_SCREEN_STOPPING);
    }

    cleanup_services(runtime);
    mybot_wifi_deinit(&runtime->wifi);

    mybot_presenter_show_screen(&runtime->presenter, previous == MYBOT_STATE_FAILED
                                                         ? MYBOT_LCD_SCREEN_FAILED
                                                         : MYBOT_LCD_SCREEN_STOPPING);
    mybot_presenter_deinit(&runtime->presenter);

    if (mybot_agora_rtc_fini() < 0) {
        AOSL_LOG_ERR("RTC fini incomplete; RTSA reported terminal cleanup failure");
    }
#if MYBOT_ENABLE_VIDEO
    if (mybot_video_destroy(&runtime->video) < 0) {
        AOSL_LOG_ERR("video source destroy incomplete");
    }
#endif
    if (mybot_media_pipeline_destroy(&runtime->media) < 0) {
        AOSL_LOG_ERR("media pipeline destroy skipped because stop was incomplete");
    }

    mybot_state_model_reset(&runtime->state_model);
    AOSL_LOG_NTC("application control stopped");
}

static void handle_control_start(const aosl_ts_t *queued_ts, aosl_refobj_t robj, uintptr_t argc,
                                 uintptr_t argv[]) {
    (void)queued_ts;
    (void)robj;
    if (argc != 1) {
        return;
    }

    mybot_runtime_t *runtime = (mybot_runtime_t *)argv[0];
    runtime->control_start_result = -1;
    if (aosl_atomic_read(&s_exit_generation) != aosl_atomic_read(&s_run_exit_generation)) {
        return;
    }
    mybot_state_model_reset(&runtime->state_model);
    if (!mybot_state_model_begin_start(&runtime->state_model)) {
        AOSL_LOG_ERR("failed to enter application start state");
        return;
    }
    aosl_atomic_set(&runtime->running, true);

    if (mybot_presenter_init(&runtime->presenter) < 0) {
        AOSL_LOG_ERR("failed to initialize application presenter");
        goto fail;
    }
    mybot_presenter_show_screen(&runtime->presenter, MYBOT_LCD_SCREEN_STARTING);
    mybot_presenter_show_screen(&runtime->presenter, MYBOT_LCD_SCREEN_WIFI_PROVISIONING);
    if (mybot_wifi_init(&runtime->wifi, runtime->config.device_id, on_wifi_event, runtime) < 0) {
        AOSL_LOG_ERR("failed to initialize Wi-Fi platform");
        goto fail;
    }
    if (aosl_atomic_read(&s_exit_generation) != aosl_atomic_read(&s_run_exit_generation)) {
        runtime_publish_exit(runtime);
        return;
    }

    runtime->control_start_result = 0;
    AOSL_LOG_NTC("application control started");
    return;

fail:
    (void)mybot_state_model_fail(&runtime->state_model);
    mybot_presenter_show_screen(&runtime->presenter, MYBOT_LCD_SCREEN_FAILED);
    runtime_publish_exit(runtime);
}

static void control_worker_fini(void *arg) {
    control_stop_runtime(arg);
}

static bool destroy_control_queue(mybot_runtime_t *runtime) {
    if (!aosl_mpq_invalid(runtime->control_mpq)) {
        if (aosl_mpq_destroy_wait(runtime->control_mpq) < 0) {
            AOSL_LOG_ERR("failed to destroy application control queue");
            return false;
        }
        runtime->control_mpq = AOSL_MPQ_INVALID;
    }
    /* Retain AOSL ownership until a failed background teardown can be retried. */
    if (!destroy_service_queue(runtime))
        return false;
    apply_completed_services(runtime);
    if (runtime->lifecycle_initialized) {
        mybot_device_lifecycle_finish_shutdown(&runtime->lifecycle);
        runtime->lifecycle_initialized = false;
        mybot_kv_store_deinit(&runtime->kv_store);
    }
    return true;
}

int mybot_start(const mybot_config_t *cfg) {
    mybot_runtime_t *runtime = &s_default_runtime;

    lifecycle_lock();

    if (aosl_atomic_read(&runtime->aosl_ref_held)) {
        AOSL_LOG_ERR("application start rejected: runtime is already active");
        lifecycle_unlock();
        return -1;
    }
    if (!config_is_valid(cfg)) {
        AOSL_LOG_ERR("application start rejected: invalid configuration");
        lifecycle_unlock();
        return -1;
    }
    if (!platform_requirements_are_met(cfg)) {
        lifecycle_unlock();
        return -1;
    }
    aosl_atomic_set(&s_run_exit_generation, aosl_atomic_read(&s_exit_generation));
    aosl_atomic_set(&runtime->running, false);
    memcpy(&runtime->config, cfg, sizeof(runtime->config));
    runtime->control_mpq = AOSL_MPQ_INVALID;
    runtime->control_timer = AOSL_MPQ_TIMER_INVALID;
    runtime->service_mpq = AOSL_MPQ_INVALID;
    runtime->completed_results = NULL;
    aosl_atomic_set(&runtime->service_result_gate, 0);
    aosl_atomic_set(&runtime->service_results_enabled, false);
    runtime->control_start_result = -1;

    runtime->ui_head = runtime->ui_count = 0;
    runtime->ui_generation = ++s_ui_generation;
    aosl_atomic_set(&runtime->ui_gate, 0);
    aosl_atomic_set(&runtime->ui_accepting_commands, false);

    aosl_ctor();
    if (aosl_get_log_level() < AOSL_LOG_NOTICE) {
        aosl_set_log_level(AOSL_LOG_NOTICE);
    }
    aosl_atomic_set(&runtime->aosl_ref_held, true);

    /* Callbacks must not wait for space while this worker synchronously waits for RTC. */
    runtime->control_mpq = aosl_mpq_create_flags(AOSL_MPQ_FLAG_SIGP_EVENT | AOSL_MPQ_FLAG_NONBLOCK,
                                                 AOSL_THRD_PRI_NORMAL, CONTROL_MPQ_STACK_SIZE, 1000,
                                                 "control_mpq", NULL, control_worker_fini, runtime);
    if (aosl_mpq_invalid(runtime->control_mpq)) {
        AOSL_LOG_ERR("failed to create application control queue");
        goto fail;
    }
    if (aosl_mpq_call(runtime->control_mpq, AOSL_REF_INVALID, "handle_control_start",
                      handle_control_start, 1, (uintptr_t)runtime) < 0) {
        AOSL_LOG_ERR("failed to run application control startup");
        goto fail;
    }
    if (runtime->control_start_result < 0) {
        goto fail;
    }
    lifecycle_unlock();
    return 0;

fail:
    runtime_publish_exit(runtime);
    /* Queue destruction runs cleanup on the control worker, even when its queue is full. */
    if (destroy_control_queue(runtime)) {
        aosl_dtor();
        aosl_atomic_set(&runtime->aosl_ref_held, false);
    }
    lifecycle_unlock();
    return -1;
}

bool mybot_is_running(void) {
    return runtime_is_running(&s_default_runtime);
}

mybot_state_t mybot_get_state(void) {
    return runtime_get_state(&s_default_runtime);
}

void mybot_stop(void) {
    mybot_runtime_t *runtime = &s_default_runtime;
    lifecycle_lock();
    if (!aosl_atomic_read(&runtime->aosl_ref_held)) {
        lifecycle_unlock();
        return;
    }

    runtime_publish_exit(runtime);
    /* Cleanup belongs to the control finalizer; it needs no separate queue admission. */
    if (destroy_control_queue(runtime)) {
        aosl_dtor();
        aosl_atomic_set(&runtime->aosl_ref_held, false);
    }
    lifecycle_unlock();
}
