/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../components/mybot_stack/mybot_sdk/mybot/src/core/mybot_app.c"

#define runtime s_default_runtime
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        assert(condition);                                                                         \
        ++checks;                                                                                  \
    } while (0)

static unsigned checks, cases, warnings, last_invalid, last_dropped;
static int allocations, fail_after = -1;
typedef enum {
    INJECT_NONE,
    INJECT_CLOSE,
    INJECT_GENERATION,
    INJECT_EXIT,
    INJECT_EXIT_GENERATION,
    INJECT_GATE
} injection_t;
static injection_t inject_on_allocation;

void test_log(const char *format, ...) {
    if (strcmp(format, "event=caption invalid=%u dropped=%u"))
        return;
    va_list args;
    va_start(args, format);
    last_invalid = va_arg(args, unsigned);
    last_dropped = va_arg(args, unsigned);
    va_end(args);
    warnings++;
}

void aosl_msleep(unsigned milliseconds) {
    (void)milliseconds;
    assert(!"caption receive and UI reads must never wait");
}

void *aosl_hal_malloc(size_t size) {
    injection_t injection = inject_on_allocation;
    inject_on_allocation = INJECT_NONE;
    switch (injection) {
    case INJECT_CLOSE:
        runtime.caption_accepting = false;
        break;
    case INJECT_GENERATION:
        aosl_atomic_inc(&runtime.conversation_generation);
        break;
    case INJECT_EXIT:
        runtime_publish_exit(&runtime);
        break;
    case INJECT_EXIT_GENERATION:
        aosl_atomic_inc(&s_exit_generation);
        break;
    case INJECT_GATE:
        runtime.caption_inbox_gate = 1;
        break;
    case INJECT_NONE:
        break;
    }
    if (fail_after == 0)
        return NULL;
    if (fail_after > 0)
        --fail_after;
    void *value = malloc(size);
    if (value)
        allocations++;
    return value;
}

void aosl_hal_free(void *value) {
    if (value)
        allocations--;
    free(value);
}

static const char *original = "{\"object\":\"user.transcription\",\"stream_id\":1,\"turn_id\":1,"
                              "\"text\":\"你好\",\"final\":true}";
static const char *translated = "{\"object\":\"user.translation\",\"stream_id\":1,\"turn_id\":1,"
                                "\"text\":\"Hello\",\"final\":true}";

static void reset(void) {
    if (runtime.captions) {
        runtime.caption_inbox_gate = runtime.caption_model_gate = 0;
        close_caption_inbox(&runtime, false);
        mybot_captions_destroy(runtime.captions);
    }
    CHECK(allocations == 0);
    memset(&runtime, 0, sizeof(runtime));
    s_lifecycle_gate = s_exit_generation = s_run_exit_generation = 0;
    runtime.running = 1;
    runtime.conversation_generation = 7;
    runtime.captions = mybot_captions_create();
    CHECK(runtime.captions && allocations == 1);
    mybot_captions_reset(runtime.captions, 7);
    runtime.caption_accepting = true;
    strcpy(runtime.rtc_channel, "translation-channel");
    strcpy(runtime.rtc_agent_uid[0], "translation-agent");
    warnings = last_invalid = last_dropped = 0;
    fail_after = -1;
    inject_on_allocation = INJECT_NONE;
}

static void direct(const char *json) {
    rtc_on_caption_direct("translation-agent", json, strlen(json), "json", &runtime);
}

static mybot_caption_t read_caption(unsigned sequence) {
    mybot_caption_t value;
    memset(&value, 0, sizeof(value));
    CHECK(mybot_ui_copy_caption(7, sequence, &value) == 0);
    return value;
}

static void test_authenticated_direct_channel_and_copy(void) {
    reset();
    size_t length = strlen(original);
    receive_caption(&runtime, NULL, NULL, original, length);
    receive_caption(&runtime, NULL, "foreign-agent", original, length);
    receive_caption(&runtime, "foreign-channel", "translation-agent", original, length);
    rtc_on_caption_channel(NULL, "translation-agent", original, length, NULL, &runtime);
    rtc_on_caption_channel("foreign-channel", "translation-agent", original, length, NULL,
                           &runtime);
    rtc_on_caption_direct("translation-agent", NULL, length, NULL, &runtime);
    rtc_on_caption_direct("translation-agent", original, 0, NULL, &runtime);
    rtc_on_caption_direct("translation-agent", original, MYBOT_CAPTION_MESSAGE_CAPACITY + 1, NULL,
                          &runtime);
    runtime.caption_inbox_gate = 1;
    direct(original);
    runtime.caption_inbox_gate = 0;
    CHECK(runtime.caption_count == 0 && allocations == 1);

    char borrowed[256];
    snprintf(borrowed, sizeof(borrowed), "%s", original);
    rtc_on_caption_direct("translation-agent", borrowed, strlen(borrowed), "json", &runtime);
    memset(borrowed, 'x', strlen(borrowed));
    rtc_on_caption_channel("translation-channel", "translation-agent", translated,
                           strlen(translated), "json", &runtime);
    CHECK(runtime.caption_count == 2 && allocations == 3);
    CHECK(drain_caption_inbox(&runtime));
    CHECK(runtime.caption_count == 0 && allocations == 1);
    mybot_caption_t value = read_caption(1);
    CHECK(!strcmp(value.source_text, "你好") && !strcmp(value.target_text, "Hello"));
    CHECK(value.source_final && value.target_final);
    refresh_caption_metadata(&runtime);
    CHECK(runtime.presenter.captions.generation == 7 && runtime.presenter.captions.count == 1);
    CHECK(runtime.presenter.captions.first_sequence == 1 &&
          runtime.presenter.captions.latest_sequence == 1);
    CHECK(!drain_caption_inbox(&runtime) && !warnings);
    cases++;
}

static void test_payload_nul_and_parser_rejections(void) {
    reset();
    rtc_on_caption_direct("translation-agent", original, strlen(original) + 1, NULL, &runtime);
    CHECK(drain_caption_inbox(&runtime));
    CHECK(!strcmp(read_caption(1).source_text, "你好"));
    char invalid[256];
    snprintf(invalid, sizeof(invalid), "%s", translated);
    size_t length = strlen(invalid);
    invalid[length + 1] = '\0';
    rtc_on_caption_direct("translation-agent", invalid, length + 2, NULL, &runtime);
    invalid[10] = '\0';
    rtc_on_caption_direct("translation-agent", invalid, length, NULL, &runtime);
    direct("{\"object\":\"user.translation\",\"stream_id\":1,\"turn_id\":1,\"text\":\"\xc0\x80\"}");
    direct("{\"object\":\"user.translation\",\"stream_id\":1,\"turn_id\":1,\"text\":true}");
    CHECK(!drain_caption_inbox(&runtime));
    CHECK(warnings == 1 && last_invalid == 4 && last_dropped == 0);
    CHECK(!read_caption(1).target_text[0] && allocations == 1);
    cases++;
}

static void test_bounded_inbox_discards_oldest(void) {
    reset();
    for (unsigned i = 1; i <= 10; ++i) {
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"object\":\"user.transcription\",\"stream_id\":1,\"turn_id\":%u,"
                 "\"text\":\"turn %u\"}",
                 i, i);
        direct(json);
        CHECK(runtime.caption_count <= CAPTION_INBOX_CAPACITY);
        CHECK(allocations <= 1 + CAPTION_INBOX_CAPACITY);
    }
    CHECK(runtime.caption_count == 8 && runtime.caption_dropped == 2);
    CHECK(drain_caption_inbox(&runtime));
    CHECK(warnings == 1 && last_dropped == 2 && !last_invalid && !runtime.caption_dropped);
    refresh_caption_metadata(&runtime);
    CHECK(runtime.presenter.captions.count == 8);
    CHECK(!strcmp(read_caption(1).source_text, "turn 3"));
    CHECK(!strcmp(read_caption(8).source_text, "turn 10"));
    CHECK(allocations == 1);
    cases++;
}

static void test_generation_reset_and_pause(void) {
    reset();
    direct(original);
    runtime.conversation_generation = 8;
    mybot_captions_reset(runtime.captions, 8);
    CHECK(!drain_caption_inbox(&runtime));
    refresh_caption_metadata(&runtime);
    CHECK(!runtime.presenter.captions.count && runtime.presenter.captions.generation == 8);
    runtime.media.paused = 1;
    runtime.media_cancel_requested = 1;
    /* Pause and an audio cancellation do not discard already-produced text. */
    direct(original);
    CHECK(drain_caption_inbox(&runtime));
    mybot_caption_t value;
    CHECK(mybot_ui_copy_caption(7, 1, &value) == -1);
    CHECK(mybot_ui_copy_caption(8, 1, &value) == 0 && !strcmp(value.source_text, "你好"));
    CHECK(!warnings && allocations == 1);
    cases++;
}

static void test_close_keeps_tail_only_when_requested(void) {
    reset();
    direct(original);
    direct(translated);
    close_caption_inbox(&runtime, true);
    CHECK(!runtime.caption_accepting && !runtime.caption_count && allocations == 1);
    CHECK(!strcmp(read_caption(1).target_text, "Hello"));
    direct(original);
    CHECK(!runtime.caption_count && allocations == 1);
    refresh_caption_metadata(&runtime);
    unsigned revision = runtime.presenter.captions.revision;
    close_caption_inbox(&runtime, true);
    refresh_caption_metadata(&runtime);
    CHECK(runtime.presenter.captions.revision == revision);
    reset();
    direct(original);
    close_caption_inbox(&runtime, false);
    refresh_caption_metadata(&runtime);
    CHECK(!runtime.presenter.captions.count && allocations == 1);
    cases++;
}

static void test_public_reads_are_copied_and_nonblocking(void) {
    reset();
    direct(original);
    CHECK(drain_caption_inbox(&runtime));
    mybot_caption_t value;
    memset(&value, 0x55, sizeof(value));
    mybot_caption_t unchanged = value;
    s_lifecycle_gate = 1;
    CHECK(mybot_ui_copy_caption(7, 1, &value) == -1);
    CHECK(!memcmp(&value, &unchanged, sizeof(value)));
    s_lifecycle_gate = 0;
    runtime.caption_model_gate = 1;
    CHECK(mybot_ui_copy_caption(7, 1, &value) == -1);
    CHECK(!memcmp(&value, &unchanged, sizeof(value)) && !s_lifecycle_gate);
    runtime.caption_model_gate = 0;
    CHECK(mybot_ui_copy_caption(8, 1, &value) == -1);
    CHECK(mybot_ui_copy_caption(7, 0, &value) == -1);
    CHECK(mybot_ui_copy_caption(7, 2, &value) == -1);
    CHECK(mybot_ui_copy_caption(0, 1, &value) == -1);
    CHECK(mybot_ui_copy_caption(7, 1, NULL) == -1);
    CHECK(!memcmp(&value, &unchanged, sizeof(value)));
    CHECK(mybot_ui_copy_caption(7, 1, &value) == 0);
    strcpy(value.source_text, "caller-owned output");
    CHECK(!strcmp(read_caption(1).source_text, "你好"));
    value = unchanged;
    runtime_publish_exit(&runtime);
    CHECK(mybot_ui_copy_caption(7, 1, &value) == -1);
    CHECK(!memcmp(&value, &unchanged, sizeof(value)));
    cases++;
}

static void test_allocation_failure_preserves_history(void) {
    reset();
    fail_after = 0;
    direct(original);
    fail_after = -1;
    CHECK(!runtime.caption_count && allocations == 1 && runtime.running);
    direct(original);
    CHECK(drain_caption_inbox(&runtime));
    refresh_caption_metadata(&runtime);
    unsigned revision = runtime.presenter.captions.revision;
    bool success = false;
    for (int failure = 0; failure < 100; ++failure) {
        direct(translated);
        fail_after = failure;
        bool changed = drain_caption_inbox(&runtime);
        fail_after = -1;
        CHECK(allocations == 1 && runtime.running && runtime.caption_accepting);
        CHECK(!strcmp(read_caption(1).source_text, "你好"));
        refresh_caption_metadata(&runtime);
        if (changed) {
            success = true;
            CHECK(!strcmp(read_caption(1).target_text, "Hello"));
            break;
        }
        CHECK(!read_caption(1).target_text[0]);
        CHECK(runtime.presenter.captions.revision == revision && last_invalid == 1);
    }
    CHECK(success);
    cases++;
}

static void test_receive_revalidates_after_allocation(void) {
    static const injection_t races[] = {INJECT_CLOSE, INJECT_GENERATION, INJECT_EXIT,
                                        INJECT_EXIT_GENERATION, INJECT_GATE};
    for (size_t i = 0; i < sizeof(races) / sizeof(races[0]); ++i) {
        reset();
        inject_on_allocation = races[i];
        direct(original);
        CHECK(!runtime.caption_count && allocations == 1);
        runtime.caption_inbox_gate = 0;
    }
    reset();
    runtime_publish_exit(&runtime);
    direct(original);
    CHECK(!runtime.caption_count && allocations == 1);
    reset();
    aosl_atomic_inc(&s_exit_generation);
    CHECK(runtime.running);
    direct(original);
    CHECK(!runtime.caption_count && allocations == 1);
    cases++;
}

static void test_two_agents_share_channel_and_keep_direction(void) {
    reset();
    strcpy(runtime.rtc_agent_uid[1], "speak-agent");
    runtime.input_direction = MYBOT_TRANSLATION_DIRECTION_LISTEN;
    rtc_on_caption_channel("translation-channel", "speak-agent", original, strlen(original), NULL,
                           &runtime);
    rtc_on_caption_channel("translation-channel", "translation-agent", original, strlen(original),
                           NULL, &runtime);
    CHECK(drain_caption_inbox(&runtime));
    CHECK(read_caption(1).direction == MYBOT_TRANSLATION_DIRECTION_SPEAK);
    CHECK(read_caption(2).direction == MYBOT_TRANSLATION_DIRECTION_LISTEN);

    /* A button switch must not relabel either agent's delayed translation. */
    runtime.input_direction = MYBOT_TRANSLATION_DIRECTION_SPEAK;
    rtc_on_caption_channel("translation-channel", "translation-agent", translated,
                           strlen(translated), NULL, &runtime);
    rtc_on_caption_channel("translation-channel", "speak-agent", translated, strlen(translated),
                           NULL, &runtime);
    CHECK(drain_caption_inbox(&runtime));
    CHECK(!strcmp(read_caption(1).target_text, "Hello"));
    CHECK(!strcmp(read_caption(2).target_text, "Hello"));
    refresh_caption_metadata(&runtime);
    CHECK(runtime.presenter.captions.count == 2);
    CHECK(runtime.presenter.captions.latest_speak_sequence == 1);
    CHECK(runtime.presenter.captions.latest_listen_sequence == 2);

    rtc_on_caption_channel("foreign-channel", "speak-agent", translated, strlen(translated), NULL,
                           &runtime);
    rtc_on_caption_channel("translation-channel", "foreign-agent", translated, strlen(translated),
                           NULL, &runtime);
    CHECK(!runtime.caption_count);
    rtc_on_caption_channel("translation-channel", "speak-agent", translated, strlen(translated),
                           NULL, &runtime);
    CHECK(!drain_caption_inbox(&runtime));
    CHECK(allocations == 1);
    cases++;
}

int main(void) {
    test_authenticated_direct_channel_and_copy();
    test_payload_nul_and_parser_rejections();
    test_bounded_inbox_discards_oldest();
    test_generation_reset_and_pause();
    test_close_keeps_tail_only_when_requested();
    test_public_reads_are_copied_and_nonblocking();
    test_allocation_failure_preserves_history();
    test_receive_revalidates_after_allocation();
    test_two_agents_share_channel_and_keep_direction();
    close_caption_inbox(&runtime, false);
    mybot_captions_destroy(runtime.captions);
    runtime.captions = NULL;
    CHECK(!allocations);
    printf("PASS translation_caption_control (%u cases, %u checks)\n", cases, checks);
    return 0;
}
