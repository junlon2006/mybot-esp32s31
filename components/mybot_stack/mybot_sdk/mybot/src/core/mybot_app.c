/* SPDX-License-Identifier: Apache-2.0 */
#include <mybot/mybot.h>
#include <mybot/mybot_build_config.h>
#include <mybot/mybot_ui.h>
#include <mybot/platform/mybot_platform.h>

#include "mybot_agora_rtc.h"
#include "mybot_captions.h"
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
#define CAPTION_INBOX_CAPACITY 8
typedef struct {
    uint32_t generation;
    mybot_translation_direction_t direction;
    size_t length;
    char body[];
} caption_message_t;
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
    /* Allocated only for meetings; retained until RTC and I/O workers drain. */
    mybot_media_pipeline_t *usb_media;
    aosl_atomic_t meeting_active;
    aosl_atomic_t meeting_control_busy;
    bool media_recovery_required;
#if MYBOT_ENABLE_VIDEO
    mybot_video_t video;
#endif
    aosl_atomic_t conversation_generation;
    aosl_atomic_t media_cancel_requested;
    aosl_atomic_t audio_mode;
    aosl_atomic_t input_direction;
    /* Protect route selection through the remote PCM producer's admission. */
    aosl_atomic_t remote_route_gate;
    /* Sender-owned scratch, allocated in PSRAM only when face-to-face is used. */
    int16_t *silent_audio;
    mybot_device_lifecycle_t lifecycle;
    /* Snapshots used to reject stale RTM events from a previous conversation. */
    char rtc_channel[128];
    char rtc_agent_uid[2][MYBOT_RTM_UID_MAX_LEN];
    mybot_captions_t *captions;
    aosl_atomic_t caption_model_gate;
    aosl_atomic_t caption_inbox_gate;
    caption_message_t *caption_inbox[CAPTION_INBOX_CAPACITY];
    unsigned caption_head;
    unsigned caption_count;
    unsigned caption_dropped;
    unsigned caption_rejected;
    bool caption_accepting;

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

static void caption_lock(aosl_atomic_t *gate) {
    while (aosl_atomic_cmpxchg(gate, 0, 1) != 0)
        aosl_msleep(1);
}

/* Control owns the model. UI copies are nonblocking and never retain its memory. */
int mybot_ui_copy_caption(uint32_t generation, uint32_t sequence, mybot_caption_t *out) {
    if (!generation || !sequence || !out || aosl_atomic_cmpxchg(&s_lifecycle_gate, 0, 1) != 0)
        return -1;
    mybot_runtime_t *runtime = &s_default_runtime;
    int result = -1;
    if (aosl_atomic_cmpxchg(&runtime->caption_model_gate, 0, 1) == 0) {
        mybot_caption_state_t state = {0};
        if (runtime_is_running(runtime) && runtime->captions) {
            mybot_captions_get_state(runtime->captions, &state);
            if (state.generation == generation)
                result = mybot_captions_copy(runtime->captions, sequence, out);
        }
        aosl_atomic_set(&runtime->caption_model_gate, 0);
    }
    lifecycle_unlock();
    return result;
}

/* Inbox ownership is independent of queued AOSL notifications and survives a
 * blocked control worker. It is drained/freed explicitly before shutdown. */
static bool drain_caption_inbox(mybot_runtime_t *runtime) {
    bool changed = false;
    unsigned invalid = 0;
    for (unsigned n = 0; n < CAPTION_INBOX_CAPACITY; ++n) {
        caption_lock(&runtime->caption_inbox_gate);
        caption_message_t *message = NULL;
        if (runtime->caption_count) {
            message = runtime->caption_inbox[runtime->caption_head];
            runtime->caption_inbox[runtime->caption_head] = NULL;
            runtime->caption_head = (runtime->caption_head + 1) % CAPTION_INBOX_CAPACITY;
            runtime->caption_count--;
        }
        aosl_atomic_set(&runtime->caption_inbox_gate, 0);
        if (!message)
            break;
        if (message->generation == (uint32_t)aosl_atomic_read(&runtime->conversation_generation)) {
            caption_lock(&runtime->caption_model_gate);
            if (runtime->captions) {
                int result = mybot_captions_apply_json(runtime->captions, message->direction,
                                                       message->body, message->length);
                changed |= result > 0;
                invalid += result < 0;
            }
            aosl_atomic_set(&runtime->caption_model_gate, 0);
        }
        aosl_hal_free(message);
    }
    caption_lock(&runtime->caption_inbox_gate);
    unsigned dropped = runtime->caption_dropped;
    unsigned rejected = runtime->caption_rejected;
    runtime->caption_dropped = 0;
    runtime->caption_rejected = 0;
    aosl_atomic_set(&runtime->caption_inbox_gate, 0);
    if (invalid || dropped)
        AOSL_LOG_WRN("event=caption invalid=%u dropped=%u", invalid, dropped);
    if (rejected)
        AOSL_LOG_WRN("event=caption rejected_source_or_channel=%u", rejected);
    return changed;
}

static void close_caption_inbox(mybot_runtime_t *runtime, bool keep_received) {
    caption_lock(&runtime->caption_inbox_gate);
    runtime->caption_accepting = false;
    aosl_atomic_set(&runtime->caption_inbox_gate, 0);
    if (keep_received)
        (void)drain_caption_inbox(runtime);
    caption_lock(&runtime->caption_inbox_gate);
    while (runtime->caption_count) {
        aosl_hal_free(runtime->caption_inbox[runtime->caption_head]);
        runtime->caption_inbox[runtime->caption_head] = NULL;
        runtime->caption_head = (runtime->caption_head + 1) % CAPTION_INBOX_CAPACITY;
        runtime->caption_count--;
    }
    runtime->caption_head = runtime->caption_dropped = 0;
    runtime->caption_rejected = 0;
    aosl_atomic_set(&runtime->caption_inbox_gate, 0);
}

static void refresh_caption_metadata(mybot_runtime_t *runtime) {
    caption_lock(&runtime->caption_model_gate);
    mybot_captions_get_state(runtime->captions, &runtime->presenter.captions);
    aosl_atomic_set(&runtime->caption_model_gate, 0);
}

static void receive_caption(mybot_runtime_t *runtime, const char *channel, const char *sender,
                            const void *data, size_t length) {
    if (!sender || !data || !length || length > MYBOT_CAPTION_MESSAGE_CAPACITY ||
        aosl_atomic_cmpxchg(&runtime->caption_inbox_gate, 0, 1) != 0)
        return;
    mybot_translation_direction_t direction = MYBOT_TRANSLATION_DIRECTION_NONE;
    for (unsigned i = 0; i < 2; ++i)
        if (runtime->rtc_agent_uid[i][0] && strcmp(sender, runtime->rtc_agent_uid[i]) == 0)
            direction =
                i == 0 ? MYBOT_TRANSLATION_DIRECTION_LISTEN : MYBOT_TRANSLATION_DIRECTION_SPEAK;
    bool allowed = runtime_is_running(runtime) && runtime->caption_accepting &&
                   direction != MYBOT_TRANSLATION_DIRECTION_NONE &&
                   (!channel || strcmp(channel, runtime->rtc_channel) == 0);
    uint32_t generation = (uint32_t)aosl_atomic_read(&runtime->conversation_generation);
    if (!allowed && runtime->caption_accepting && runtime_is_running(runtime))
        runtime->caption_rejected++;
    aosl_atomic_set(&runtime->caption_inbox_gate, 0);
    if (!allowed)
        return;
    caption_message_t *message = aosl_hal_malloc(sizeof(*message) + length + 1);
    if (!message)
        return;
    message->generation = generation;
    message->direction = direction;
    message->length = length;
    memcpy(message->body, data, length);
    message->body[length] = '\0';
    if (aosl_atomic_cmpxchg(&runtime->caption_inbox_gate, 0, 1) != 0) {
        aosl_hal_free(message);
        return;
    }
    if (!runtime_is_running(runtime) || !runtime->caption_accepting ||
        generation != (uint32_t)aosl_atomic_read(&runtime->conversation_generation)) {
        aosl_atomic_set(&runtime->caption_inbox_gate, 0);
        aosl_hal_free(message);
        return;
    }
    caption_message_t *retired = NULL;
    if (runtime->caption_count == CAPTION_INBOX_CAPACITY) {
        retired = runtime->caption_inbox[runtime->caption_head];
        runtime->caption_head = (runtime->caption_head + 1) % CAPTION_INBOX_CAPACITY;
        runtime->caption_count--;
        runtime->caption_dropped++;
    }
    unsigned tail = (runtime->caption_head + runtime->caption_count) % CAPTION_INBOX_CAPACITY;
    runtime->caption_inbox[tail] = message;
    runtime->caption_count++;
    aosl_atomic_set(&runtime->caption_inbox_gate, 0);
    aosl_hal_free(retired);
}

static void rtc_on_caption_direct(const char *sender, const void *data, size_t len,
                                  const char *custom_type, void *user_data) {
    (void)custom_type;
    receive_caption(user_data, NULL, sender, data, len);
}

static void rtc_on_caption_channel(const char *channel, const char *sender, const void *data,
                                   size_t len, const char *custom_type, void *user_data) {
    (void)custom_type;
    if (channel)
        receive_caption(user_data, channel, sender, data, len);
}

static void refresh_translation_view(mybot_runtime_t *runtime);
static void lock_ui_commands(mybot_runtime_t *runtime);

static int usb_media_send_audio(const void *data, size_t len, void *user_data);
static void media_error(void *user_data);

/* Called with RTC callbacks drained, or release=false while shutdown is pending. */
static int stop_usb_media(mybot_runtime_t *runtime, bool release) {
    aosl_atomic_set(&runtime->meeting_active, false);
    if (!runtime->usb_media)
        return 0;
    if (mybot_media_pipeline_stop(runtime->usb_media) < 0 || !release)
        return -1;
    if (mybot_media_pipeline_destroy(runtime->usb_media) < 0)
        return -1;
    aosl_hal_free(runtime->usb_media);
    runtime->usb_media = NULL;
    return 0;
}

static bool end_session_media(mybot_runtime_t *runtime, bool rtc_stopped) {
    int physical = mybot_media_pipeline_end_session(&runtime->media);
    int usb = stop_usb_media(runtime, rtc_stopped);
    return physical < 0 || usb < 0;
}

static int start_usb_media(mybot_runtime_t *runtime) {
    const mybot_platform_descriptor_t *platform = mybot_platform_registry_get();
    if (!platform->audio_usb_capture || !platform->audio_usb_playback || runtime->usb_media)
        return -1;
    runtime->usb_media = aosl_hal_malloc(sizeof(*runtime->usb_media));
    if (!runtime->usb_media)
        return -1;
    mybot_media_pipeline_init(runtime->usb_media);
    mybot_media_pipeline_callbacks_t callbacks = {
        .send_audio = usb_media_send_audio,
        .on_audio_error = media_error,
        .user_data = runtime,
    };
    if (mybot_media_pipeline_start_usb(runtime->usb_media, &callbacks) < 0) {
        runtime->media_recovery_required = stop_usb_media(runtime, true) < 0;
        return -1;
    }
    aosl_atomic_set(&runtime->meeting_active, true);
    return 0;
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
        if (runtime->media_recovery_required || runtime->usb_media ||
            mybot_media_pipeline_is_paused(&runtime->media) ||
            aosl_atomic_read(&runtime->media.capture_fault) ||
            aosl_atomic_read(&runtime->media.playback_fault)) {
            runtime->media_recovery_required = end_session_media(runtime, true);
            if (runtime->media_recovery_required) {
                snprintf(runtime->lifecycle.error_code, sizeof(runtime->lifecycle.error_code),
                         "MEDIA_NOT_READY");
                AOSL_LOG_ERR("local media recovery failed; cloud translation start rejected");
                return;
            }
            if (strcmp(runtime->lifecycle.error_code, "MEDIA_NOT_READY") == 0)
                runtime->lifecycle.error_code[0] = '\0';
        }
        /* One generation spans the whole start attempt, including RTC join.
         * Key STOP admission shares this short fence with the cancellation reset. */
        lock_ui_commands(runtime);
        aosl_atomic_inc(&runtime->conversation_generation);
        aosl_atomic_set(&runtime->media_cancel_requested, false);
        aosl_atomic_set(&runtime->ui_gate, 0);
        /* A media worker can fail after preflight but before cancellation resets.
         * Its sticky fault must reject this attempt even if that reset won. */
        if (aosl_atomic_read(&runtime->media.capture_fault) ||
            aosl_atomic_read(&runtime->media.playback_fault)) {
            aosl_atomic_set(&runtime->media_cancel_requested, true);
            runtime->media_recovery_required = true;
            snprintf(runtime->lifecycle.error_code, sizeof(runtime->lifecycle.error_code),
                     "MEDIA_NOT_READY");
            refresh_translation_view(runtime);
            mybot_presenter_render_state(&runtime->presenter, &runtime->state_model);
            return;
        }
        if (runtime->lifecycle.audio_source == MYBOT_AUDIO_SOURCE_USB &&
            start_usb_media(runtime) < 0) {
            snprintf(runtime->lifecycle.error_code, sizeof(runtime->lifecycle.error_code),
                     "USB_AUDIO_NOT_READY");
            refresh_translation_view(runtime);
            mybot_presenter_render_state(&runtime->presenter, &runtime->state_model);
            return;
        }
        if (aosl_atomic_read(&runtime->media_cancel_requested)) {
            runtime->media_recovery_required = end_session_media(runtime, true);
            return;
        }
        mybot_device_lifecycle_request_start(&runtime->lifecycle);
        if (!runtime->lifecycle.conversation_requested &&
            runtime->lifecycle.audio_source == MYBOT_AUDIO_SOURCE_USB)
            runtime->media_recovery_required = end_session_media(runtime, true);
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
            request->payload.start.mode, request->payload.start.source_language,
            request->payload.start.target_language, &request->payload.start.response);
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
    if (aosl_atomic_read(&runtime->media_cancel_requested) ||
        aosl_atomic_read(&runtime->meeting_control_busy))
        return 0;
    if (aosl_atomic_read(&runtime->meeting_active))
        return mybot_agora_rtc_send_audio(MYBOT_TRANSLATION_DIRECTION_SPEAK, data, len);
    if (aosl_atomic_read(&runtime->audio_mode) == MYBOT_TRANSLATION_LISTEN)
        return mybot_agora_rtc_send_audio(MYBOT_TRANSLATION_DIRECTION_LISTEN, data, len);

    const size_t samples = MYBOT_MEDIA_FRAME_SAMPLES * (MYBOT_CLOUD_AEC ? 2U : 1U);
    if (!data || !runtime->silent_audio || len != samples * sizeof(int16_t))
        return -1;
    /* Consume the physical playback reference once in the pipeline, then give
     * both agents that same reference. Only the selected agent receives MIC. */
    const int16_t *pcm = data;
    for (size_t i = 0; i < samples; ++i)
        runtime->silent_audio[i] = MYBOT_CLOUD_AEC && (i & 1U) ? pcm[i] : 0;
    mybot_translation_direction_t selected = aosl_atomic_read(&runtime->input_direction);
    int result = 0;
    for (mybot_translation_direction_t direction = MYBOT_TRANSLATION_DIRECTION_LISTEN;
         direction <= MYBOT_TRANSLATION_DIRECTION_SPEAK; ++direction) {
        if (aosl_atomic_read(&runtime->media_cancel_requested))
            break;
        const void *frame = direction == selected ? data : runtime->silent_audio;
        if (mybot_agora_rtc_send_audio(direction, frame, len) < 0)
            result = -1;
    }
    return result;
}

static int usb_media_send_audio(const void *data, size_t len, void *user_data) {
    mybot_runtime_t *runtime = user_data;
    if (aosl_atomic_read(&runtime->media_cancel_requested) ||
        aosl_atomic_read(&runtime->meeting_control_busy) ||
        !aosl_atomic_read(&runtime->meeting_active))
        return 0;
    return mybot_agora_rtc_send_audio(MYBOT_TRANSLATION_DIRECTION_LISTEN, data, len);
}

static void media_error(void *user_data) {
    mybot_runtime_t *runtime = user_data;
    /* Close both routes through cancellation, then let control quiesce USB.
     * Its pipeline may be retiring while a physical worker reports this error. */
    aosl_atomic_set(&runtime->media_cancel_requested, true);
    mybot_media_pipeline_set_rtc_connected(&runtime->media, false);
    mybot_device_lifecycle_notify_conversation_ended(&runtime->lifecycle);
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

static void rtc_on_remote_audio(mybot_translation_direction_t direction, uint32_t uid,
                                const void *data, size_t len, void *user_data) {
    (void)uid;
    mybot_runtime_t *runtime = user_data;
    if (aosl_atomic_cmpxchg(&runtime->remote_route_gate, 0, 1) != 0)
        return;
    if (!aosl_atomic_read(&runtime->media_cancel_requested)) {
        if (aosl_atomic_read(&runtime->meeting_active)) {
            if (direction == MYBOT_TRANSLATION_DIRECTION_LISTEN)
                mybot_media_pipeline_push_remote_audio(&runtime->media, data, len);
            else if (direction == MYBOT_TRANSLATION_DIRECTION_SPEAK && runtime->usb_media)
                mybot_media_pipeline_push_remote_audio(runtime->usb_media, data, len);
        } else if (direction == aosl_atomic_read(&runtime->input_direction)) {
            mybot_media_pipeline_push_remote_audio(&runtime->media, data, len);
        }
    }
    aosl_atomic_set(&runtime->remote_route_gate, 0);
}

static void rtc_on_state_changed(mybot_rtc_state_t state, void *user_data) {
    mybot_runtime_t *runtime = user_data;
    bool connected =
        state == MYBOT_RTC_STATE_CONNECTED && !aosl_atomic_read(&runtime->media_cancel_requested);
    mybot_media_pipeline_set_rtc_connected(&runtime->media, connected);
    if (runtime->usb_media)
        mybot_media_pipeline_set_rtc_connected(runtime->usb_media, connected);
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
    if (!runtime_is_running(runtime) || aosl_atomic_read(&runtime->media_cancel_requested) ||
        !params || params->route_count < 1 || params->route_count > 2) {
        mybot_device_lifecycle_notify_conversation_ended(&runtime->lifecycle);
        return;
    }
    close_caption_inbox(runtime, false);
    caption_lock(&runtime->caption_model_gate);
    mybot_captions_reset(runtime->captions,
                         (uint32_t)aosl_atomic_read(&runtime->conversation_generation));
    aosl_atomic_set(&runtime->caption_model_gate, 0);
    refresh_caption_metadata(runtime);
    if (mybot_media_pipeline_flush_session(&runtime->media) < 0 ||
        (runtime->usb_media && mybot_media_pipeline_flush_session(runtime->usb_media) < 0)) {
        AOSL_LOG_ERR("failed to flush media session before RTC join");
        mybot_device_lifecycle_notify_conversation_ended(&runtime->lifecycle);
        return;
    }
    if (params->mode == MYBOT_TRANSLATION_LISTEN_SPEAK &&
        !aosl_atomic_read(&runtime->meeting_active) && !runtime->silent_audio) {
        runtime->silent_audio =
            aosl_hal_malloc(MYBOT_MEDIA_FRAME_BYTES * (MYBOT_CLOUD_AEC ? 2U : 1U));
        if (!runtime->silent_audio) {
            snprintf(runtime->lifecycle.error_code, sizeof(runtime->lifecycle.error_code),
                     "MEDIA_NOT_READY");
            mybot_device_lifecycle_notify_conversation_ended(&runtime->lifecycle);
            return;
        }
    }
    aosl_atomic_set(&runtime->audio_mode, params->mode);
    aosl_atomic_set(&runtime->input_direction, params->mode == MYBOT_TRANSLATION_LISTEN
                                                   ? MYBOT_TRANSLATION_DIRECTION_LISTEN
                                                   : MYBOT_TRANSLATION_DIRECTION_NONE);

    mybot_agora_rtc_callbacks_t callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    if (params->routes[0].rtc_agent_uid[0] == '\0') {
        AOSL_LOG_ERR("conversation response missing RTM agent UID");
        mybot_device_lifecycle_notify_conversation_ended(&runtime->lifecycle);
        return;
    }
    caption_lock(&runtime->caption_inbox_gate);
    snprintf(runtime->rtc_channel, sizeof(runtime->rtc_channel), "%s",
             params->routes[0].rtc_channel);
    memset(runtime->rtc_agent_uid, 0, sizeof(runtime->rtc_agent_uid));
    for (unsigned i = 0; i < params->route_count; ++i)
        snprintf(runtime->rtc_agent_uid[i], sizeof(runtime->rtc_agent_uid[i]), "%s",
                 params->routes[i].rtc_agent_uid);
    runtime->caption_accepting = runtime->captions != NULL;
    aosl_atomic_set(&runtime->caption_inbox_gate, 0);
    callbacks.on_remote_audio = rtc_on_remote_audio;
    callbacks.on_state_changed = rtc_on_state_changed;
    callbacks.on_token_will_expire = rtc_on_token_will_expire;
    callbacks.on_rtm_event = rtc_on_rtm_event;
    callbacks.on_rtm_subscribe_result = rtc_on_rtm_subscribe_result;
    callbacks.on_rtm_data = rtc_on_caption_direct;
    callbacks.on_rtm_subscribe_data = rtc_on_caption_channel;
#if MYBOT_ENABLE_VIDEO
    callbacks.on_video_key_frame_requested = rtc_on_video_key_frame_requested;
    callbacks.on_video_target_bitrate_changed = rtc_on_video_target_bitrate_changed;
#endif
    callbacks.user_data = runtime;

    if (mybot_agora_rtc_init(params->routes[0].rtc_app_id, &callbacks) < 0) {
        AOSL_LOG_ERR("failed to initialize Agora RTC");
        mybot_device_lifecycle_notify_conversation_ended(&runtime->lifecycle);
        return;
    }

    AOSL_LOG_NTC("joining translation RTC routes=%u", params->route_count);
    if (mybot_agora_rtc_join(params->routes, params->route_count) < 0) {
        AOSL_LOG_ERR("failed to join Agora RTC channel");
        mybot_device_lifecycle_notify_conversation_ended(&runtime->lifecycle);
        return;
    }
    AOSL_LOG_NTC("RTC join requested");
}

static void dev_on_conversation_stop(void *user_data) {
    mybot_runtime_t *runtime = user_data;
    aosl_atomic_set(&runtime->media_cancel_requested, true);
    aosl_atomic_set(&runtime->input_direction, MYBOT_TRANSLATION_DIRECTION_NONE);
    if (runtime->usb_media) {
        /* Clear both device queues before any potentially slow RTC finalization.
         * A disconnect already closed rtc_connected; quiesce still owns playback. */
        int physical = aosl_atomic_read(&runtime->media.running)
                           ? mybot_media_pipeline_quiesce(&runtime->media)
                           : 0;
        int usb = aosl_atomic_read(&runtime->usb_media->running)
                      ? mybot_media_pipeline_quiesce(runtime->usb_media)
                      : 0;
        if (physical < 0 || usb < 0)
            AOSL_LOG_WRN("meeting audio quiesce incomplete; session cleanup will retry");
    }
    if (strcmp(runtime->lifecycle.error_code, "MODE_CHANGE_FAILED") == 0 ||
        strcmp(runtime->lifecycle.error_code, "DIRECTION_CHANGE_FAILED") == 0)
        runtime->lifecycle.error_code[0] = '\0';
    close_caption_inbox(runtime, true);
    refresh_caption_metadata(runtime);
    lock_ui_commands(runtime);
    aosl_atomic_inc(&runtime->conversation_generation);
    aosl_atomic_set(&runtime->ui_gate, 0);
    caption_lock(&runtime->caption_inbox_gate);
    runtime->rtc_channel[0] = '\0';
    memset(runtime->rtc_agent_uid, 0, sizeof(runtime->rtc_agent_uid));
    aosl_atomic_set(&runtime->caption_inbox_gate, 0);
#if MYBOT_ENABLE_VIDEO
    if (mybot_video_stop(&runtime->video) < 0) {
        AOSL_LOG_ERR("failed to stop video source");
    }
#endif
    /* Render from the current state snapshot before the lifecycle publishes its
     * next device state, so the old conversation overlay cannot win a race. */
    mybot_presenter_render_state(&runtime->presenter, &runtime->state_model);
    /* RTSA belongs to this conversation, including its App ID and callback queue. */
    bool rtc_stopped = mybot_agora_rtc_fini() == 0;
    if (!rtc_stopped) {
        const char *error = runtime->lifecycle.error_code;
        if (!error[0] || strcmp(error, "START_UNKNOWN") == 0 ||
            strcmp(error, "CLEANUP_PENDING") == 0 || strcmp(error, "CLEANUP_AUTH_REJECTED") == 0)
            snprintf(runtime->lifecycle.error_code, sizeof(runtime->lifecycle.error_code),
                     "RTC_NOT_READY");
        AOSL_LOG_ERR("failed to finalize RTC conversation");
    }
    runtime->media_recovery_required = end_session_media(runtime, rtc_stopped);
    if (runtime->media_recovery_required)
        AOSL_LOG_ERR("local media teardown incomplete; the next start must recover audio first");
}

static void refresh_translation_view(mybot_runtime_t *runtime) {
    refresh_caption_metadata(runtime);
    int volume = -1;
    (void)mybot_media_pipeline_get_volume(&runtime->media, &volume);
    runtime->volume_retry_ticks = 0;
    mybot_presenter_set_translation(&runtime->presenter, runtime->lifecycle.source_language,
                                    runtime->lifecycle.target_language,
                                    runtime->lifecycle.error_code, volume);
    runtime->presenter.media_ready = aosl_atomic_read(&runtime->media.rtc_connected) != 0;
    runtime->presenter.paused = mybot_media_pipeline_is_paused(&runtime->media);
    runtime->presenter.translation_mode = runtime->lifecycle.mode;
    runtime->presenter.audio_source = runtime->lifecycle.audio_source;
    runtime->presenter.usb_audio_available =
        mybot_platform_registry_get()->audio_usb_capture != NULL &&
        mybot_platform_registry_get()->audio_usb_playback != NULL;
    runtime->presenter.input_direction = aosl_atomic_read(&runtime->input_direction);
}

static bool ui_command_valid(const mybot_ui_command_t *command) {
    if (!command)
        return false;
    switch (command->type) {
    case MYBOT_UI_SET_VOLUME:
        return command->value >= 0 && command->value <= 100;
    case MYBOT_UI_SET_PAUSED:
        return command->value == 0 || command->value == 1;
    case MYBOT_UI_SET_MODE:
        if ((command->value != MYBOT_TRANSLATION_LISTEN &&
             command->value != MYBOT_TRANSLATION_LISTEN_SPEAK) ||
            (command->audio_source != MYBOT_AUDIO_SOURCE_CODEC &&
             command->audio_source != MYBOT_AUDIO_SOURCE_USB) ||
            (command->audio_source == MYBOT_AUDIO_SOURCE_USB &&
             (command->value != MYBOT_TRANSLATION_LISTEN_SPEAK ||
              !mybot_platform_registry_get()->audio_usb_capture ||
              !mybot_platform_registry_get()->audio_usb_playback)))
            return false;
        if (!command->source_language[0] && !command->target_language[0])
            return true;
        /* The optional pair is validated by the same rule as language edits. */
        {
            mybot_ui_command_t languages = *command;
            languages.type = MYBOT_UI_SET_LANGUAGES;
            return ui_command_valid(&languages);
        }
    case MYBOT_UI_SET_DIRECTION:
        return command->value == MYBOT_TRANSLATION_DIRECTION_LISTEN ||
               command->value == MYBOT_TRANSLATION_DIRECTION_SPEAK;
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

static bool media_controls_allowed(mybot_runtime_t *runtime) {
    return runtime_get_state(runtime) == MYBOT_STATE_IN_CONVERSATION &&
           aosl_atomic_read(&runtime->media.rtc_connected) &&
           !aosl_atomic_read(&runtime->media_cancel_requested) &&
           !aosl_atomic_read(&runtime->lifecycle.stop_request);
}

static int control_set_direction(mybot_runtime_t *runtime,
                                 mybot_translation_direction_t direction) {
    if (!media_controls_allowed(runtime) ||
        runtime->lifecycle.mode != MYBOT_TRANSLATION_LISTEN_SPEAK ||
        aosl_atomic_read(&runtime->meeting_active))
        return -1;
    mybot_translation_direction_t previous = aosl_atomic_read(&runtime->input_direction);
    if (previous == direction && !mybot_media_pipeline_is_paused(&runtime->media))
        return 0;

    /* Remote producers never wait. Fence their route check as well as the
     * pipeline queues so a callback admitted before the switch cannot publish
     * old-direction audio after playback resumes. */
    unsigned attempts = 0;
    while (aosl_atomic_cmpxchg(&runtime->remote_route_gate, 0, 1) != 0) {
        if (++attempts >= 50)
            return -1;
        aosl_msleep(1);
    }
    int result = -1;
    if (media_controls_allowed(runtime) &&
        mybot_media_pipeline_set_paused(&runtime->media, true) == 0) {
        if (media_controls_allowed(runtime)) {
            aosl_atomic_set(&runtime->input_direction, direction);
            result = mybot_media_pipeline_set_paused(&runtime->media, false);
            if (result < 0)
                aosl_atomic_set(&runtime->input_direction, previous);
        }
    }
    aosl_atomic_set(&runtime->remote_route_gate, 0);
    return result;
}

static int control_set_paused(mybot_runtime_t *runtime, bool paused) {
    if (!media_controls_allowed(runtime))
        return -1;
    if (!aosl_atomic_read(&runtime->meeting_active)) {
        if (runtime->lifecycle.mode != MYBOT_TRANSLATION_LISTEN &&
            aosl_atomic_read(&runtime->input_direction) == MYBOT_TRANSLATION_DIRECTION_NONE)
            return -1;
        return mybot_media_pipeline_set_paused(&runtime->media, paused);
    }
    unsigned attempts = 0;
    while (aosl_atomic_cmpxchg(&runtime->remote_route_gate, 0, 1) != 0) {
        if (++attempts >= 50)
            return -1;
        aosl_msleep(1);
    }
    aosl_atomic_set(&runtime->meeting_control_busy, true);
    int result = -1;
    if (media_controls_allowed(runtime) && runtime->usb_media) {
        int physical = mybot_media_pipeline_set_paused(&runtime->media, paused);
        int usb = mybot_media_pipeline_set_paused(runtime->usb_media, paused);
        if (physical == 0 && usb == 0 && media_controls_allowed(runtime)) {
            result = 0;
        } else {
            /* A partial resume never leaves one direction audible. */
            (void)mybot_media_pipeline_set_paused(&runtime->media, true);
            (void)mybot_media_pipeline_set_paused(runtime->usb_media, true);
        }
    }
    aosl_atomic_set(&runtime->meeting_control_busy, false);
    aosl_atomic_set(&runtime->remote_route_gate, 0);
    return result;
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
        result = control_set_paused(runtime, command.value != 0);
        break;
    case MYBOT_UI_SET_MODE:
        result = mybot_device_lifecycle_set_selection(
            &runtime->lifecycle, command.value, command.audio_source,
            command.source_language[0] ? command.source_language : NULL,
            command.target_language[0] ? command.target_language : NULL);
        if (result == 0)
            aosl_atomic_set(&runtime->input_direction, MYBOT_TRANSLATION_DIRECTION_NONE);
        break;
    case MYBOT_UI_SET_DIRECTION:
        result = control_set_direction(runtime, command.value);
        break;
    }
    if (command.type == MYBOT_UI_SET_MODE || command.type == MYBOT_UI_SET_DIRECTION) {
        if (++runtime->presenter.selection_revision == 0)
            ++runtime->presenter.selection_revision;
        if (result < 0 && !aosl_atomic_read(&runtime->media_cancel_requested) &&
            !aosl_atomic_read(&runtime->lifecycle.stop_request)) {
            snprintf(runtime->lifecycle.error_code, sizeof(runtime->lifecycle.error_code), "%s",
                     command.type == MYBOT_UI_SET_MODE ? "MODE_CHANGE_FAILED"
                                                       : "DIRECTION_CHANGE_FAILED");
        } else if (result == 0 &&
                   (strcmp(runtime->lifecycle.error_code, "MODE_CHANGE_FAILED") == 0 ||
                    strcmp(runtime->lifecycle.error_code, "DIRECTION_CHANGE_FAILED") == 0)) {
            runtime->lifecycle.error_code[0] = '\0';
        }
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

static void release_unclaimed_usb_media(mybot_runtime_t *runtime) {
    /* Includes allocation/queue failures before STARTING and rejected/unknown
     * HTTP starts which never acquire a local RTC session. */
    if (!runtime->lifecycle.conversation_requested && !runtime->lifecycle.start_pending &&
        !runtime->lifecycle.local_media_active && runtime->usb_media)
        runtime->media_recovery_required = end_session_media(runtime, mybot_agora_rtc_fini() == 0);
}

static void dev_on_state_changed(mybot_device_state_t state, void *user_data) {
    mybot_runtime_t *runtime = user_data;
    if (!runtime_is_running(runtime)) {
        return;
    }
    (void)mybot_state_model_set_device_state(&runtime->state_model, state);
    release_unclaimed_usb_media(runtime);
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
    if (argc != 4) {
        return;
    }

    mybot_runtime_t *runtime = (mybot_runtime_t *)argv[0];
    if (!runtime_is_running(runtime) || argv[2] != runtime->ui_generation ||
        argv[3] != (uintptr_t)aosl_atomic_read(&runtime->conversation_generation)) {
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
    if (event == MYBOT_KEY_EVENT_EXIT) {
        runtime_publish_exit(runtime);
        return;
    }
    /* Match UI submission: never publish into a queue that start/stop owns. */
    if (aosl_atomic_cmpxchg(&s_lifecycle_gate, 0, 1) != 0)
        return;
    if (!runtime_is_running(runtime) || aosl_mpq_invalid(runtime->control_mpq)) {
        lifecycle_unlock();
        return;
    }
    if (aosl_atomic_cmpxchg(&runtime->ui_gate, 0, 1) != 0) {
        lifecycle_unlock();
        return;
    }
    uintptr_t ui_generation = runtime->ui_generation;
    uintptr_t conversation_generation =
        (uintptr_t)aosl_atomic_read(&runtime->conversation_generation);
    if (event == MYBOT_KEY_EVENT_CONVERSATION_STOP) {
        mybot_state_t state = runtime_get_state(runtime);
        if (state != MYBOT_STATE_IN_CONVERSATION && state != MYBOT_STATE_TRANSLATION_STARTING &&
            state != MYBOT_STATE_CLEANUP_PENDING && state != MYBOT_STATE_TRANSLATION_STOPPING) {
            aosl_atomic_set(&runtime->ui_gate, 0);
            lifecycle_unlock();
            return;
        }
        aosl_atomic_set(&runtime->media_cancel_requested, true);
    }

    if (aosl_mpq_queue(runtime->control_mpq, AOSL_MPQ_INVALID, AOSL_REF_INVALID, "handle_key_event",
                       handle_key_event, 4, (uintptr_t)runtime, (uintptr_t)event, ui_generation,
                       conversation_generation) < 0) {
        fail_control_queue(runtime, "key");
    }
    aosl_atomic_set(&runtime->ui_gate, 0);
    lifecycle_unlock();
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
        release_unclaimed_usb_media(runtime);
        bool captions_changed = drain_caption_inbox(runtime);
        bool ready = aosl_atomic_read(&runtime->media.rtc_connected) != 0;
        bool media_changed = ready != runtime->presenter.media_ready;
        /* Recover an unavailable read without polling healthy hardware volume controls. */
        bool retry_volume = runtime->presenter.volume < 0 &&
                            ++runtime->volume_retry_ticks >= VOLUME_READ_RETRY_TICKS;
        if (media_changed || retry_volume) {
            refresh_translation_view(runtime);
        } else if (captions_changed) {
            refresh_caption_metadata(runtime);
        }
        if (media_changed || captions_changed || (retry_volume && runtime->presenter.volume >= 0))
            mybot_presenter_render_state(&runtime->presenter, &runtime->state_model);
    }
}

static void cleanup_services(mybot_runtime_t *runtime) {
    close_caption_inbox(runtime, false);
    close_ui_commands(runtime);
    aosl_atomic_set(&runtime->service_results_enabled, false);
    mybot_key_deinit(&runtime->key);

    if (!aosl_mpq_timer_invalid(runtime->control_timer)) {
        if (aosl_mpq_kill_timer(runtime->control_timer) < 0) {
            AOSL_LOG_ERR("failed to stop device lifecycle timer");
        }
        runtime->control_timer = AOSL_MPQ_TIMER_INVALID;
    }

    if (runtime->media.initialized && mybot_media_pipeline_stop(&runtime->media) < 0) {
        AOSL_LOG_ERR("media pipeline stop incomplete");
    }
    if (runtime->usb_media && mybot_media_pipeline_stop(runtime->usb_media) < 0)
        AOSL_LOG_ERR("USB media pipeline stop incomplete");
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

/* Control's finalizer and later stop calls share the same retained ownership.
 * Join every producer before freeing PCM or releasing the last AOSL reference. */
static bool finish_media_shutdown(mybot_runtime_t *runtime) {
    bool stopped = true;
    if (runtime->media.initialized && mybot_media_pipeline_stop(&runtime->media) < 0)
        stopped = false;
    if (runtime->usb_media && mybot_media_pipeline_stop(runtime->usb_media) < 0)
        stopped = false;
#if MYBOT_ENABLE_VIDEO
    if (mybot_video_stop(&runtime->video) < 0)
        stopped = false;
#endif
    if (mybot_agora_rtc_fini() < 0)
        stopped = false;
    if (!stopped) {
        AOSL_LOG_ERR("media shutdown incomplete; workers and buffers retained");
        return false;
    }
    if (runtime->media.initialized && mybot_media_pipeline_destroy(&runtime->media) < 0)
        return false;
    if (stop_usb_media(runtime, true) < 0)
        return false;
#if MYBOT_ENABLE_VIDEO
    if (mybot_video_destroy(&runtime->video) < 0)
        return false;
#endif
    aosl_hal_free(runtime->silent_audio);
    runtime->silent_audio = NULL;
    return true;
}

static int start_services(mybot_runtime_t *runtime) {
    if (mybot_kv_store_init(&runtime->kv_store) < 0 ||
        mybot_key_init(&runtime->key, on_key_event, runtime) < 0) {
        goto fail;
    }

    mybot_media_pipeline_callbacks_t media_cbs;
    memset(&media_cbs, 0, sizeof(media_cbs));
    media_cbs.send_audio = media_send_audio;
    media_cbs.on_audio_error = media_error;
    media_cbs.on_wake_word = media_on_wake_word;
    media_cbs.user_data = runtime;
    mybot_media_pipeline_init(&runtime->media);
    if (mybot_media_pipeline_start(&runtime->media, &media_cbs) < 0) {
        goto fail;
    }
    runtime->media_recovery_required = false;
    caption_lock(&runtime->caption_model_gate);
    runtime->captions = mybot_captions_create();
    aosl_atomic_set(&runtime->caption_model_gate, 0);
    if (!runtime->captions)
        AOSL_LOG_WRN("event=caption result=unavailable reason=no_memory");

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
    (void)finish_media_shutdown(runtime);
    caption_lock(&runtime->caption_model_gate);
    mybot_captions_destroy(runtime->captions);
    runtime->captions = NULL;
    aosl_atomic_set(&runtime->caption_model_gate, 0);
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

    (void)finish_media_shutdown(runtime);

    caption_lock(&runtime->caption_model_gate);
    mybot_captions_destroy(runtime->captions);
    runtime->captions = NULL;
    aosl_atomic_set(&runtime->caption_model_gate, 0);
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
    }
    if (runtime->kv_store.ctx)
        mybot_kv_store_deinit(&runtime->kv_store);
    /* A repeated stop joins retained physical, USB and RTC workers even after
     * the control worker itself has already exited. */
    if (!finish_media_shutdown(runtime))
        return false;
    mybot_state_model_reset(&runtime->state_model);
    AOSL_LOG_NTC("application control stopped");
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

int mybot_stop(void) {
    mybot_runtime_t *runtime = &s_default_runtime;
    lifecycle_lock();
    if (!aosl_atomic_read(&runtime->aosl_ref_held)) {
        lifecycle_unlock();
        return 0;
    }

    runtime_publish_exit(runtime);
    /* Cleanup belongs to the control finalizer; it needs no separate queue admission. */
    bool complete = destroy_control_queue(runtime);
    if (complete) {
        aosl_dtor();
        aosl_atomic_set(&runtime->aosl_ref_held, false);
    }
    lifecycle_unlock();
    return complete ? 0 : -1;
}
