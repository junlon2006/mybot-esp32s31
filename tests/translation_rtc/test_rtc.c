/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the actual production callbacks and join/leave implementation. */
#include "../../components/mybot_stack/mybot_sdk/mybot/src/rtc/mybot_agora_rtc.c"

typedef struct {
    aosl_mpq_func_t callback;
    uintptr_t argument;
} queued_callback_t;
static queued_callback_t queue[64];
static unsigned queued, allocations, audio_callbacks, errors, unmute_calls, leave_calls;
static unsigned init_calls, fini_calls, destroy_calls, queue_creates, queue_destroys;
static unsigned tests;
static unsigned direct_messages, channel_messages;
typedef struct {
    char uid[MYBOT_RTM_UID_MAX_LEN];
    char channel[AGORA_RTC_CHANNEL_NAME_MAX_LEN + 1];
    char custom[64];
    unsigned char data[AGORA_RTM_DATA_MAX_LEN];
    size_t length;
} received_message_t;
static received_message_t direct_message, channel_message;
static connection_id_t next_connection = 40;
static aosl_mpq_t next_queue = 2, live_queue;
static rtc_channel_options_t joined_options;
static int unmute_result, join_result, destroy_result, fini_result, queue_destroy_result;
static bool lookup_ready;
static bool callbacks_during_fini;
static connection_id_t fini_connection;
static char initialized_app_id[64];
static uint32_t last_unmuted;
static int16_t last_audio;
static uint64_t tick;
static const char agent[] = "translation-agent";
static const char publisher[] = "translation-publisher";
static void drain(void);

void test_log(const char *format, ...) {
    (void)format;
}
int aosl_get_log_level(void) {
    return 0;
}
int aosl_set_log_level(int level) {
    (void)level;
    return 0;
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
aosl_ts_t aosl_tick_ms(void) {
    return tick;
}
void aosl_msleep(unsigned milliseconds) {
    tick += milliseconds;
}

int aosl_mpq_queue(aosl_mpq_t target, aosl_mpq_t source, aosl_ref_t ref, const char *name,
                   aosl_mpq_func_t callback, uintptr_t argc, ...) {
    assert(target == live_queue);
    (void)source;
    (void)ref;
    (void)name;
    assert(argc == 1 && queued < 64);
    va_list arguments;
    va_start(arguments, argc);
    queue[queued++] = (queued_callback_t){callback, va_arg(arguments, uintptr_t)};
    va_end(arguments);
    return 0;
}
int aosl_mpq_call_argv(aosl_mpq_t q, aosl_ref_t ref, const char *name, aosl_mpq_func_t callback,
                       uintptr_t argc, uintptr_t argv[]) {
    assert(q == live_queue);
    (void)ref;
    (void)name;
    /* Synchronous calls run after earlier callback work on the same worker. */
    drain();
    callback(NULL, NULL, argc, argv);
    return 0;
}
aosl_mpq_t aosl_mpq_create_flags(int flags, int priority, int stack, int maximum, const char *name,
                                 void *init, void *fini, void *argument) {
    (void)flags;
    (void)priority;
    (void)stack;
    (void)maximum;
    (void)name;
    (void)init;
    (void)fini;
    (void)argument;
    assert(live_queue == AOSL_MPQ_INVALID);
    queue_creates++;
    live_queue = next_queue++;
    return live_queue;
}
int aosl_mpq_destroy_wait(aosl_mpq_t q) {
    assert(q == live_queue);
    queue_destroys++;
    if (queue_destroy_result < 0)
        return queue_destroy_result;
    drain();
    live_queue = AOSL_MPQ_INVALID;
    return 0;
}

const char *agora_rtc_get_version(void) {
    return "host";
}
const char *agora_rtc_err_2_str(int error) {
    (void)error;
    return "injected error";
}
int agora_rtc_init(const char *app_id, const agora_rtc_event_handler_t *handler,
                   rtc_service_option_t *options) {
    init_calls++;
    snprintf(initialized_app_id, sizeof(initialized_app_id), "%s", app_id);
    (void)handler;
    (void)options;
    return 0;
}
int agora_rtc_fini(void) {
    fini_calls++;
    if (callbacks_during_fini) {
        /* A callback already dispatched by RTSA can reach the wrapper while
         * vendor teardown waits for its workers to finish. */
        user_info_t target = {.uid = 900};
        strcpy(target.user_account, publisher);
        on_user_offline(fini_connection, &target, 0);
        on_error(fini_connection, 1, "late teardown error");
        on_join_channel_success(fini_connection, 5, 1);
    }
    return fini_result;
}
int agora_rtc_create_connection(connection_id_t *connection) {
    *connection = next_connection++;
    return 0;
}
int agora_rtc_destroy_connection(connection_id_t connection) {
    (void)connection;
    destroy_calls++;
    return destroy_result;
}
int agora_rtc_join_channel_with_user_account(connection_id_t connection, const char *channel,
                                             const char *account, const char *token,
                                             rtc_channel_options_t *options) {
    assert(strcmp(channel, "translation-channel") == 0);
    assert(strcmp(account, "device-input") == 0);
    assert(strcmp(token, "route-token") == 0);
    joined_options = *options;
    if (join_result == 0)
        on_join_channel_success(connection, 15, 20);
    return join_result;
}
int agora_rtc_leave_channel(connection_id_t connection) {
    (void)connection;
    leave_calls++;
    return 0;
}
int agora_rtc_get_user_info_by_user_account(connection_id_t connection, const char *account,
                                            user_info_t *user) {
    assert(connection == s_rtc.conn_id);
    assert(strcmp(account, publisher) == 0);
    if (!lookup_ready)
        return -1;
    memset(user, 0, sizeof(*user));
    user->uid = 900;
    strcpy(user->user_account, account);
    return 0;
}
int agora_rtc_mute_remote_audio(connection_id_t connection, uint32_t uid, bool mute) {
    assert(connection == s_rtc.conn_id && !mute);
    last_unmuted = uid;
    unmute_calls++;
    return unmute_result;
}
int agora_rtc_send_audio_data(connection_id_t connection, const void *data, size_t length,
                              audio_frame_info_t *info) {
    (void)connection;
    (void)data;
    (void)length;
    (void)info;
    return 0;
}
int agora_rtm_login(const char *uid, const char *token, const agora_rtm_handler_t *handler) {
    assert(strcmp(uid, "device-input") == 0 && strcmp(token, "route-token") == 0);
    handler->on_rtm_event(uid, RTM_EVENT_TYPE_LOGIN, ERR_RTM_OK);
    return 0;
}
int agora_rtm_logout(void) {
    return 0;
}
int agora_rtm_subscribe(const char *channel) {
    on_rtm_subscribe_result(channel, ERR_RTM_OK);
    return 0;
}
int agora_rtm_unsubscribe(const char *channel) {
    (void)channel;
    return 0;
}

static void drain(void) {
    while (queued) {
        queued_callback_t event = queue[0];
        memmove(queue, queue + 1, --queued * sizeof(queue[0]));
        event.callback(NULL, NULL, 1, &event.argument);
    }
    assert(allocations == 0);
}
static void audio_received(uint32_t uid, const void *data, size_t length, void *user) {
    (void)user;
    assert(uid == s_rtc.publisher_uid && length == MYBOT_RTC_PCM_FRAME_BYTES);
    last_audio = *(const int16_t *)data;
    audio_callbacks++;
}
static void copy_message(received_message_t *message, const char *channel, const char *uid,
                         const void *data, size_t length, const char *custom, void *user) {
    assert(user == &tests && uid && data && length <= sizeof(message->data));
    snprintf(message->uid, sizeof(message->uid), "%s", uid);
    snprintf(message->channel, sizeof(message->channel), "%s", channel ? channel : "");
    snprintf(message->custom, sizeof(message->custom), "%s", custom ? custom : "");
    memcpy(message->data, data, length);
    message->length = length;
}
static void direct_received(const char *uid, const void *data, size_t length, const char *custom,
                            void *user) {
    copy_message(&direct_message, NULL, uid, data, length, custom, user);
    direct_messages++;
}
static void channel_received(const char *channel, const char *uid, const void *data, size_t length,
                             const char *custom, void *user) {
    copy_message(&channel_message, channel, uid, data, length, custom, user);
    channel_messages++;
}
static void state_changed(mybot_rtc_state_t state, void *user) {
    (void)user;
    if (state == MYBOT_RTC_STATE_ERROR)
        errors++;
}
static user_info_t user(uint32_t uid, const char *account) {
    user_info_t result = {0};
    result.uid = uid;
    strcpy(result.user_account, account);
    return result;
}
static void reset(void) {
    drain();
    memset(&s_rtc, 0, sizeof(s_rtc));
    s_rtc.state = MYBOT_RTC_STATE_INITIALIZED;
    s_rtc.conn_id = CONNECTION_ID_INVALID;
    s_rtc.callbacks.on_remote_audio = audio_received;
    s_rtc.callbacks.on_state_changed = state_changed;
    s_rtc_mpq_id = 1;
    live_queue = 1;
    s_rtc_callbacks_enabled = 1;
    s_rtc_callback_gate = 0;
    s_rtm_login_done = s_rtm_login_ok = s_rtm_sub_done = s_rtm_sub_ok = 0;
    errors = audio_callbacks = unmute_calls = leave_calls = 0;
    direct_messages = channel_messages = 0;
    memset(&direct_message, 0, sizeof(direct_message));
    memset(&channel_message, 0, sizeof(channel_message));
    init_calls = fini_calls = destroy_calls = queue_creates = queue_destroys = 0;
    unmute_result = join_result = destroy_result = fini_result = queue_destroy_result = 0;
    initialized_app_id[0] = '\0';
    callbacks_during_fini = false;
    lookup_ready = false;
}
static void reset_public(void) {
    reset();
    s_rtc.state = MYBOT_RTC_STATE_IDLE;
    memset(&s_rtc.callbacks, 0, sizeof(s_rtc.callbacks));
    s_rtc_mpq_id = AOSL_MPQ_INVALID;
    live_queue = AOSL_MPQ_INVALID;
    s_rtc_callbacks_enabled = 0;
}
static void init_public(const char *app_id) {
    const mybot_agora_rtc_callbacks_t callbacks = {.on_remote_audio = audio_received,
                                                   .on_state_changed = state_changed,
                                                   .on_rtm_data = direct_received,
                                                   .on_rtm_subscribe_data = channel_received,
                                                   .user_data = &tests};
    assert(mybot_agora_rtc_init(app_id, &callbacks) == 0);
    assert(s_rtc.state == MYBOT_RTC_STATE_INITIALIZED);
    assert(strcmp(initialized_app_id, app_id) == 0);
}
static void join_public(void) {
    assert(mybot_agora_rtc_join("translation-channel", "route-token", "device-input", agent,
                                publisher) == 0);
    drain();
    assert(s_rtc.state == MYBOT_RTC_STATE_CONNECTED);
}
static void join(void) {
    assert(rtc_join_impl("translation-channel", "route-token", "device-input", agent, publisher) ==
           0);
    drain();
    assert(s_rtc.state == MYBOT_RTC_STATE_CONNECTED);
}
static void send_audio(connection_id_t connection, uint32_t uid) {
    int16_t samples[MYBOT_RTC_PCM_FRAME_BYTES / sizeof(int16_t)] = {1234};
    audio_frame_info_t info = {.data_type = AUDIO_DATA_TYPE_PCM};
    on_audio_data(connection, uid, 0, samples, sizeof(samples), &info);
    samples[0] = 5678; /* The vendor callback buffer is borrowed and immediately reused. */
}

static void test_options(void) {
    reset();
    join();
    assert(!joined_options.auto_subscribe_audio && !joined_options.auto_subscribe_video);
    assert(joined_options.enable_audio_decode && joined_options.enable_audio_jitter_buffer);
    assert(joined_options.enable_audio_downlink_aec == (bool)MYBOT_CLOUD_AEC);
    assert(joined_options.enable_audio_ai_qos == (bool)MYBOT_AI_QOS);
    assert(joined_options.audio_codec_opt.audio_codec_type == AUDIO_CODEC_TYPE_G722);
    assert(joined_options.audio_codec_opt.pcm_sample_rate == 16000);
    assert(joined_options.audio_codec_opt.pcm_channel_num == 1);
    assert(joined_options.audio_codec_opt.pcm_duration == 60);
    assert(!s_rtc.publisher_subscribed && unmute_calls == 0);
    tests++;
}
static void test_whitelist(void) {
    reset();
    join();
    user_info_t stranger = user(800, "untrusted-user"), target = user(900, publisher);
    on_user_joined(s_rtc.conn_id, &stranger, 0);
    drain();
    assert(unmute_calls == 0);
    send_audio(s_rtc.conn_id, 900);
    drain();
    assert(audio_callbacks == 0);
    on_user_info_updated(s_rtc.conn_id, &target);
    drain();
    assert(unmute_calls == 1 && last_unmuted == 900 && s_rtc.publisher_subscribed);
    on_user_joined(s_rtc.conn_id, &target, 0);
    drain();
    assert(unmute_calls == 1);
    send_audio(s_rtc.conn_id, 800);
    drain();
    assert(audio_callbacks == 0);
    send_audio(s_rtc.conn_id, 900);
    drain();
    assert(audio_callbacks == 1 && last_audio == 1234);
    tests++;
}
static void test_lookup(void) {
    reset();
    lookup_ready = true;
    join();
    assert(unmute_calls == 1 && last_unmuted == 900);
    tests++;
}
static void test_offline(void) {
    const char *required[] = {agent, publisher};
    for (unsigned i = 0; i < 2; i++) {
        reset();
        lookup_ready = true;
        join();
        user_info_t irrelevant = user(800, "irrelevant"), lost = user(900, required[i]);
        on_user_offline(s_rtc.conn_id, &irrelevant, 0);
        drain();
        assert(errors == 0);
        on_user_offline(s_rtc.conn_id, &lost, 0);
        drain();
        assert(errors == 1 && s_rtc.state == MYBOT_RTC_STATE_ERROR);
        send_audio(s_rtc.conn_id, 900);
        drain();
        assert(audio_callbacks == 0);
        tests++;
    }
}
static void test_late_callbacks(void) {
    reset();
    lookup_ready = true;
    join();
    connection_id_t old = s_rtc.conn_id;
    user_info_t target = user(900, publisher);
    send_audio(old, 900);
    on_user_offline(old, &target, 0);
    assert(rtc_leave_impl() == 0 && leave_calls == 1);
    drain();
    assert(errors == 0 && audio_callbacks == 0);
    join();
    unsigned count = unmute_calls;
    send_audio(old, 900);
    on_user_info_updated(old, &target);
    on_user_offline(old, &target, 0);
    on_error(old, 1, "late error");
    on_join_channel_success(old, 5, 1);
    drain();
    assert(audio_callbacks == 0 && errors == 0 && unmute_calls == count);
    assert(s_rtc.state == MYBOT_RTC_STATE_CONNECTED);
    send_audio(s_rtc.conn_id, 900);
    drain();
    assert(audio_callbacks == 1);
    tests++;
}
static void test_disabled_callbacks(void) {
    reset();
    lookup_ready = true;
    join();
    set_callbacks_enabled(false);
    user_info_t target = user(900, publisher);
    send_audio(s_rtc.conn_id, 900);
    on_user_offline(s_rtc.conn_id, &target, 0);
    assert(queued == 0 && allocations == 0 && errors == 0 && audio_callbacks == 0);
    tests++;
}
static void test_unmute_failure(void) {
    reset();
    join();
    unmute_result = -1;
    user_info_t target = user(900, publisher);
    on_user_joined(s_rtc.conn_id, &target, 0);
    drain();
    assert(errors == 1 && !s_rtc.publisher_subscribed);
    send_audio(s_rtc.conn_id, 900);
    drain();
    assert(audio_callbacks == 0);
    tests++;
}
static void test_join_failure(void) {
    reset();
    join_result = -1;
    assert(rtc_join_impl("translation-channel", "route-token", "device-input", agent, publisher) <
           0);
    drain();
    assert(s_rtc.conn_id == CONNECTION_ID_INVALID);
    assert(s_rtc.state == MYBOT_RTC_STATE_INITIALIZED);
    assert(!s_rtc.rtm_login_requested && !s_rtc.rtm_subscribe_requested);
    tests++;
}
static void test_project_change_after_fini(void) {
    reset_public();
    init_public("project-A");
    assert(mybot_agora_rtc_init("project-A", NULL) < 0 && init_calls == 1);
    join_public();
    assert(mybot_agora_rtc_fini() == 0);
    assert(s_rtc.state == MYBOT_RTC_STATE_IDLE);
    assert(s_rtc.conn_id == CONNECTION_ID_INVALID);
    assert(s_rtc_mpq_id == AOSL_MPQ_INVALID && !s_rtc_callbacks_enabled);
    assert(fini_calls == 1 && destroy_calls == 1 && queue_destroys == 1);
    assert(mybot_agora_rtc_fini() == 0 && fini_calls == 1);
    init_public("project-B");
    join_public();
    assert(init_calls == 2 && queue_creates == 2);
    assert(mybot_agora_rtc_fini() == 0);
    tests++;
}
static void test_destroy_failure_recovered_by_fini(void) {
    for (unsigned failure = 0; failure < 3; failure++) {
        reset_public();
        init_public("project-A");
        if (failure == 2) {
            join_result = destroy_result = -1;
            assert(mybot_agora_rtc_join("translation-channel", "route-token", "device-input", agent,
                                        publisher) < 0);
            drain();
            assert(s_rtc.state == MYBOT_RTC_STATE_ERROR);
        } else {
            join_public();
            destroy_result = -1;
            if (failure == 1) {
                assert(rtc_leave_impl() < 0);
                assert(s_rtc.state == MYBOT_RTC_STATE_ERROR);
            }
        }
        /* Whole-service teardown releases RTSA even if its per-connection
         * destroy failed, including a failed join's partial connection. */
        assert(mybot_agora_rtc_fini() == 0);
        assert(s_rtc.state == MYBOT_RTC_STATE_IDLE);
        assert(s_rtc_mpq_id == AOSL_MPQ_INVALID);
        assert(fini_calls == 1 && queue_destroys == 1);
        assert(destroy_calls == 1);
        join_result = destroy_result = 0;
        init_public("project-B");
        join_public();
        assert(mybot_agora_rtc_fini() == 0);
        tests++;
    }
}
static void test_failed_fini_is_terminal(void) {
    for (unsigned queue_failure = 0; queue_failure < 2; queue_failure++) {
        reset_public();
        init_public("project-A");
        join_public();
        fini_result = -1;
        queue_destroy_result = queue_failure ? -1 : 0;
        assert(mybot_agora_rtc_fini() < 0);
        assert(s_rtc.state == MYBOT_RTC_STATE_ERROR);
        assert(!s_rtc_callbacks_enabled);
        assert(fini_calls == 1);
        unsigned previous_queue_creates = queue_creates;
        assert(mybot_agora_rtc_fini() < 0);
        assert(mybot_agora_rtc_init("project-B", NULL) < 0);
        assert(init_calls == 1 && fini_calls == 1 && queue_creates == previous_queue_creates);
        assert(s_rtc.state == MYBOT_RTC_STATE_ERROR && !s_rtc_callbacks_enabled);
        /* Retiring a previously retained forwarding queue must neither retry
         * the vendor fini nor erase its terminal failure. */
        queue_destroy_result = 0;
        assert(mybot_agora_rtc_fini() < 0);
        assert(s_rtc_mpq_id == AOSL_MPQ_INVALID && fini_calls == 1);
        assert(mybot_agora_rtc_fini() < 0 && fini_calls == 1);
        tests++;
    }
}
static void test_fini_drains_old_session_callbacks(void) {
    reset_public();
    init_public("project-A");
    lookup_ready = true;
    join_public();
    connection_id_t old = s_rtc.conn_id;
    send_audio(old, 900);
    assert(queued != 0);
    callbacks_during_fini = true;
    fini_connection = old;
    assert(mybot_agora_rtc_fini() == 0);
    assert(queued == 0 && allocations == 0);
    assert(errors == 0);
    /* Callbacks in flight during teardown or between sessions are rejected. */
    send_audio(old, 900);
    on_error(old, 1, "old session between init calls");
    assert(queued == 0 && allocations == 0);
    audio_callbacks = 0;
    callbacks_during_fini = false;
    /* RTSA may assign the same connection ID after full reinitialization. */
    next_connection = old;
    init_public("project-B");
    join_public();
    assert(s_rtc.conn_id == old && errors == 0 && audio_callbacks == 0);
    send_audio(s_rtc.conn_id, 900);
    drain();
    assert(audio_callbacks == 1);
    assert(mybot_agora_rtc_fini() == 0);
    tests++;
}
static void test_forwarding_queue_destroy_retry(void) {
    reset_public();
    init_public("project-A");
    join_public();
    queue_destroy_result = -1;
    assert(mybot_agora_rtc_fini() < 0);
    assert(s_rtc.state == MYBOT_RTC_STATE_IDLE);
    assert(s_rtc_mpq_id != AOSL_MPQ_INVALID && !s_rtc_callbacks_enabled);
    assert(fini_calls == 1 && queue_destroys == 1);
    assert(mybot_agora_rtc_init("project-B", NULL) < 0);
    assert(init_calls == 1 && queue_creates == 1 && !s_rtc_callbacks_enabled);
    queue_destroy_result = 0;
    assert(mybot_agora_rtc_fini() == 0);
    assert(fini_calls == 1 && queue_destroys == 2);
    assert(s_rtc_mpq_id == AOSL_MPQ_INVALID);
    init_public("project-B");
    join_public();
    assert(mybot_agora_rtc_fini() == 0);
    tests++;
}
static const char caption_json[] =
    "{\"object\":\"user.translation\",\"text\":\"你好，world\",\"final\":true}";

static void test_rtm_copied_payload(rtm_message_type_e type) {
    reset_public();
    init_public("caption-project");
    join_public();
    assert(s_rtc.rtm_logged_in && s_rtc.rtm_subscribed);
    char uid[MYBOT_RTM_UID_MAX_LEN], channel[AGORA_RTC_CHANNEL_NAME_MAX_LEN + 1];
    char custom[64], payload[sizeof(caption_json)];
    strcpy(uid, agent);
    strcpy(channel, "translation-channel");
    strcpy(custom, "caption-json");
    memcpy(payload, caption_json, sizeof(payload));
    const size_t length = sizeof(caption_json) - 1; /* No terminating NUL is delivered. */
    on_rtm_data(uid, payload, length, type, custom);
    on_rtm_subscribe_data(channel, uid, payload, length, type, custom);
    assert(queued == 2 && direct_messages == 0 && channel_messages == 0);
    /* RTSA reuses every borrowed argument as soon as its callback returns. */
    memset(uid, 'x', strlen(uid));
    memset(channel, 'x', strlen(channel));
    memset(custom, 'x', strlen(custom));
    memset(payload, 'x', sizeof(payload));
    drain();
    assert(direct_messages == 1 && channel_messages == 1);
    assert(direct_message.length == length && channel_message.length == length);
    assert(memcmp(direct_message.data, caption_json, length) == 0);
    assert(memcmp(channel_message.data, caption_json, length) == 0);
    assert(strcmp(direct_message.uid, agent) == 0 && strcmp(channel_message.uid, agent) == 0);
    assert(strcmp(direct_message.custom, "caption-json") == 0);
    assert(strcmp(channel_message.custom, "caption-json") == 0);
    assert(strcmp(channel_message.channel, "translation-channel") == 0);
    assert(mybot_agora_rtc_fini() == 0);
    tests++;
}
static void test_rtm_binary_bytes(void) {
    reset_public();
    init_public("caption-project");
    join_public();
    const unsigned char expected[] = {'{', 0, 0xff, '}'};
    unsigned char payload[sizeof(expected)];
    memcpy(payload, expected, sizeof(payload));
    on_rtm_data(agent, payload, sizeof(payload), RTM_MESSAGE_TYPE_BINARY, NULL);
    on_rtm_subscribe_data("translation-channel", agent, payload, sizeof(payload),
                          RTM_MESSAGE_TYPE_BINARY, NULL);
    memset(payload, 'x', sizeof(payload));
    drain();
    /* The wrapper preserves bytes. The application separately validates UTF-8 JSON. */
    assert(direct_messages == 1 && channel_messages == 1);
    assert(direct_message.length == sizeof(expected) && channel_message.length == sizeof(expected));
    assert(memcmp(direct_message.data, expected, sizeof(expected)) == 0);
    assert(memcmp(channel_message.data, expected, sizeof(expected)) == 0);
    assert(!direct_message.custom[0] && !channel_message.custom[0]);
    assert(mybot_agora_rtc_fini() == 0);
    tests++;
}
static void test_rtm_channel_and_type_filters(void) {
    reset_public();
    init_public("caption-project");
    join_public();
    const size_t length = sizeof(caption_json) - 1;
    on_rtm_subscribe_data("wrong-channel", agent, caption_json, length, RTM_MESSAGE_TYPE_STRING,
                          NULL);
    assert(queued == 0 && allocations == 0);
    on_rtm_data(agent, caption_json, length, (rtm_message_type_e)999, NULL);
    on_rtm_subscribe_data("translation-channel", agent, caption_json, length,
                          (rtm_message_type_e)999, NULL);
    drain();
    assert(direct_messages == 0 && channel_messages == 0);
    /* A channel can change after callback capture but before its forwarding work. */
    on_rtm_subscribe_data("translation-channel", agent, caption_json, length,
                          RTM_MESSAGE_TYPE_STRING, NULL);
    strcpy(s_rtc.rtm_channel, "new-channel");
    drain();
    assert(channel_messages == 0);
    strcpy(s_rtc.rtm_channel, "translation-channel");
    assert(mybot_agora_rtc_fini() == 0);
    tests++;
}
static void test_rtm_stale_generations(void) {
    reset_public();
    init_public("caption-project");
    join_public();
    const size_t length = sizeof(caption_json) - 1;
    const uint32_t old_login = s_rtc.rtm_login_generation;
    const uint32_t old_subscription = s_rtc.rtm_subscribe_generation;
    on_rtm_data(agent, caption_json, length, RTM_MESSAGE_TYPE_STRING, NULL);
    on_rtm_subscribe_data("translation-channel", agent, caption_json, length,
                          RTM_MESSAGE_TYPE_BINARY, NULL);
    assert(queued == 2);
    /* Rejoin on the owner while old callback work is still deferred. */
    assert(rtc_leave_impl() == 0);
    assert(rtc_join_impl("translation-channel", "route-token", "device-input", agent, publisher) ==
           0);
    assert(s_rtc.rtm_login_generation != old_login);
    assert(s_rtc.rtm_subscribe_generation != old_subscription);
    drain();
    assert(direct_messages == 0 && channel_messages == 0);
    on_rtm_data(agent, caption_json, length, RTM_MESSAGE_TYPE_BINARY, NULL);
    on_rtm_subscribe_data("translation-channel", agent, caption_json, length,
                          RTM_MESSAGE_TYPE_STRING, NULL);
    drain();
    assert(direct_messages == 1 && channel_messages == 1);
    assert(mybot_agora_rtc_fini() == 0);
    tests++;
}
static void test_rtm_callback_shutdown(void) {
    reset_public();
    init_public("caption-project");
    join_public();
    const size_t length = sizeof(caption_json) - 1;
    set_callbacks_enabled(false);
    on_rtm_data(agent, caption_json, length, RTM_MESSAGE_TYPE_STRING, NULL);
    on_rtm_subscribe_data("translation-channel", agent, caption_json, length,
                          RTM_MESSAGE_TYPE_BINARY, NULL);
    assert(queued == 0 && allocations == 0);
    assert(direct_messages == 0 && channel_messages == 0);
    assert(mybot_agora_rtc_fini() == 0);
    on_rtm_data(agent, caption_json, length, RTM_MESSAGE_TYPE_BINARY, NULL);
    on_rtm_subscribe_data("translation-channel", agent, caption_json, length,
                          RTM_MESSAGE_TYPE_STRING, NULL);
    assert(queued == 0 && allocations == 0);
    assert(direct_messages == 0 && channel_messages == 0);
    tests++;
}
int main(void) {
    test_options();
    test_whitelist();
    test_lookup();
    test_offline();
    test_late_callbacks();
    test_disabled_callbacks();
    test_unmute_failure();
    test_join_failure();
    test_project_change_after_fini();
    test_destroy_failure_recovered_by_fini();
    test_failed_fini_is_terminal();
    test_fini_drains_old_session_callbacks();
    test_forwarding_queue_destroy_retry();
    test_rtm_copied_payload(RTM_MESSAGE_TYPE_STRING);
    test_rtm_copied_payload(RTM_MESSAGE_TYPE_BINARY);
    test_rtm_binary_bytes();
    test_rtm_channel_and_type_filters();
    test_rtm_stale_generations();
    test_rtm_callback_shutdown();
    reset();
    printf("translation RTC: %u source-level whitelist/lifecycle/RTM scenarios passed\n", tests);
    return 0;
}
