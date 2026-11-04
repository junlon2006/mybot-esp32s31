/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_captions.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static int allocations;
static int fail_after = -1;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        assert(condition);                                                                         \
        ++checks;                                                                                  \
    } while (0)

void *aosl_hal_malloc(size_t size) {
    if (fail_after == 0)
        return NULL;
    if (fail_after > 0)
        --fail_after;
    void *value = malloc(size);
    if (value)
        ++allocations;
    return value;
}

void aosl_hal_free(void *value) {
    if (value)
        --allocations;
    free(value);
}

static int apply(mybot_captions_t *captions, const char *json) {
    return mybot_captions_apply_json(captions, json, strlen(json));
}

static mybot_caption_state_t state(mybot_captions_t *captions) {
    mybot_caption_state_t value;
    mybot_captions_get_state(captions, &value);
    return value;
}

static mybot_caption_t record(mybot_captions_t *captions, unsigned sequence) {
    mybot_caption_t value;
    memset(&value, 0, sizeof(value));
    CHECK(mybot_captions_copy(captions, sequence, &value) == 0);
    return value;
}

static void expect_invalid(mybot_captions_t *captions, const char *json) {
    mybot_caption_state_t before = state(captions);
    CHECK(apply(captions, json) == -1);
    mybot_caption_state_t after = state(captions);
    CHECK(!memcmp(&before, &after, sizeof(before)));
    CHECK(allocations == 1);
}

static void test_pairing(mybot_captions_t *captions) {
    const char *translation = "{\"object\":\"user.translation\",\"text\":\"Hello everyone.\","
                              "\"final\":false,\"start_ms\":0,\"duration_ms\":1200,"
                              "\"stream_id\":1,\"turn_id\":1,\"transcript_text\":\"大家好。\","
                              "\"metadata\":null,\"words\":[],\"language\":\"en-US\"}";
    CHECK(apply(captions, translation) == 1);
    mybot_caption_t value = record(captions, 1);
    CHECK(!strcmp(value.source_text, "大家好。"));
    CHECK(!strcmp(value.target_text, "Hello everyone."));
    CHECK(!value.source_final && !value.target_final);
    unsigned revision = state(captions).revision;
    CHECK(apply(captions, translation) == 0);
    CHECK(state(captions).revision == revision);
    CHECK(apply(captions, "{\"object\":\"user.transcription\",\"text\":\"你好。\","
                          "\"final\":true,\"stream_id\":\"1\",\"turn_id\":\"1\"}") == 1);
    value = record(captions, 1);
    CHECK(!strcmp(value.source_text, "你好。") && value.source_final);
    CHECK(state(captions).count == 1);
    CHECK(apply(captions, "{\"object\":\"user.translation\",\"text\":\"Hello.\","
                          "\"final\":true,\"stream_id\":1.0,\"turn_id\":1e0,"
                          "\"transcript_text\":\"不得覆盖\"}") == 1);
    value = record(captions, 1);
    CHECK(!strcmp(value.source_text, "你好。"));
    CHECK(!strcmp(value.target_text, "Hello.") && value.target_final);
    CHECK(apply(captions, "{\"object\":\"user.translation\",\"text\":\"stale\","
                          "\"final\":false,\"stream_id\":1,\"turn_id\":1}") == 0);
    CHECK(!strcmp(record(captions, 1).target_text, "Hello."));
    CHECK(apply(captions, "{\"object\":\"user.transcription\",\"text\":\"new stream\","
                          "\"stream_id\":2,\"turn_id\":1}") == 1);
    CHECK(state(captions).count == 2);
}

static void test_revisions(mybot_captions_t *captions) {
    mybot_captions_reset(captions, 2);
    CHECK(apply(captions,
                "{\"object\":\"user.transcription\",\"text\":\"long original\","
                "\"stream_id\":1,\"turn_id\":1,\"start_ms\":10,\"duration_ms\":40}") == 1);
    CHECK(apply(captions,
                "{\"object\":\"user.transcription\",\"text\":\"short\","
                "\"stream_id\":1,\"turn_id\":1,\"start_ms\":10,\"duration_ms\":41}") == 1);
    CHECK(!strcmp(record(captions, 1).source_text, "short"));
    CHECK(apply(captions, "{\"object\":\"user.transcription\",\"text\":\"older final\","
                          "\"final\":true,\"stream_id\":1,\"turn_id\":1,"
                          "\"start_ms\":0,\"duration_ms\":50}") == 0);
    CHECK(!record(captions, 1).source_final);
    CHECK(apply(captions, "{\"object\":\"user.translation\",\"text\":\"译文\","
                          "\"stream_id\":1,\"turn_id\":1,\"start_ms\":0,\"duration_ms\":2}") == 1);
    CHECK(!strcmp(record(captions, 1).target_text, "译文"));
    CHECK(apply(captions, "{\"object\":\"user.transcription\",\"text\":\"equal time\","
                          "\"stream_id\":1,\"turn_id\":1,\"start_ms\":0,\"duration_ms\":51}") == 1);
    CHECK(apply(captions, "{\"object\":\"user.transcription\",\"text\":\"no time\","
                          "\"stream_id\":1,\"turn_id\":1}") == 1);
    CHECK(apply(captions, "{\"object\":\"user.transcription\",\"text\":\"still older\","
                          "\"stream_id\":1,\"turn_id\":1,\"start_ms\":0,\"duration_ms\":50}") == 0);
    CHECK(apply(captions, "{\"object\":\"user.transcription\",\"text\":\"\","
                          "\"stream_id\":1,\"turn_id\":1,\"final\":true}") == 1);
    CHECK(record(captions, 1).source_final);
    CHECK(!record(captions, 1).source_text[0]);
    CHECK(apply(captions, "{\"object\":\"user.translation\",\"text\":\"译\","
                          "\"transcript_text\":\"fallback must not replace actual empty text\","
                          "\"stream_id\":1,\"turn_id\":1}") == 1);
    CHECK(!record(captions, 1).source_text[0]);
}

static void test_ring(mybot_captions_t *captions) {
    mybot_captions_reset(captions, 3);
    char json[256];
    for (unsigned i = 1; i <= 65; ++i) {
        (void)snprintf(json, sizeof(json),
                       "{\"object\":\"user.transcription\",\"stream_id\":1,\"turn_id\":%u,"
                       "\"text\":\"turn %u\"}",
                       i, i);
        CHECK(apply(captions, json) == 1);
    }
    mybot_caption_state_t current = state(captions);
    CHECK(current.count == 64 && current.first_sequence == 2 && current.latest_sequence == 65);
    mybot_caption_t value;
    memset(&value, 0x55, sizeof(value));
    mybot_caption_t before = value;
    CHECK(mybot_captions_copy(captions, 1, &value) == -1);
    CHECK(!memcmp(&value, &before, sizeof(value)));
    CHECK(mybot_captions_copy(captions, 0, &value) == -1);
    CHECK(mybot_captions_copy(captions, 66, &value) == -1);
    CHECK(apply(captions, "{\"object\":\"user.transcription\",\"stream_id\":1,\"turn_id\":2,"
                          "\"text\":\"old turn revision\"}") == 1);
    CHECK(state(captions).latest_sequence == 65);
    CHECK(!strcmp(record(captions, 2).source_text, "old turn revision"));
    CHECK(!strcmp(record(captions, 65).source_text, "turn 65"));
    unsigned revision = state(captions).revision;
    mybot_captions_reset(captions, 4);
    current = state(captions);
    CHECK(current.generation == 4 && current.revision != revision && !current.count);
    CHECK(!current.first_sequence && !current.latest_sequence);
    revision = current.revision;
    mybot_captions_reset(captions, 5);
    CHECK(state(captions).revision != revision);
}

static void test_limits(mybot_captions_t *captions) {
    mybot_captions_reset(captions, 6);
    char json[8193];
    char text[1200];
    memset(text, 'a', 1022);
    strcpy(text + 1022, "你");
    (void)snprintf(
        json, sizeof(json),
        "{\"object\":\"user.transcription\",\"stream_id\":1,\"turn_id\":1,\"text\":\"%s\"}", text);
    CHECK(apply(captions, json) == 1);
    mybot_caption_t value = record(captions, 1);
    CHECK(strlen(value.source_text) == 1022 && value.source_truncated);
    memset(text, 'b', 1020);
    strcpy(text + 1020, "你");
    (void)snprintf(
        json, sizeof(json),
        "{\"object\":\"user.transcription\",\"stream_id\":1,\"turn_id\":1,\"text\":\"%s\"}", text);
    CHECK(apply(captions, json) == 1);
    value = record(captions, 1);
    CHECK(strlen(value.source_text) == 1023 && !value.source_truncated);
    CHECK(apply(captions, "{\"object\":\"user.transcription\",\"stream_id\":1,\"turn_id\":1,"
                          "\"text\":\"\\u4f60\\ud83d\\ude00\"}") == 1);
    CHECK(!strcmp(record(captions, 1).source_text, "你😀"));

    const char *base =
        "{\"object\":\"user.transcription\",\"stream_id\":1,\"turn_id\":1,\"text\":\"limit\"}";
    size_t length = strlen(base);
    memcpy(json, base, length);
    memset(json + length, ' ', sizeof(json) - length);
    CHECK(mybot_captions_apply_json(captions, json, 8192) == 1);
    CHECK(mybot_captions_apply_json(captions, json, 8193) == -1);
    CHECK(mybot_captions_apply_json(captions, base, length + 1) == 0);
    memcpy(json, base, length + 1);
    json[length + 1] = '\0';
    CHECK(mybot_captions_apply_json(captions, json, length + 2) == -1);
    json[10] = '\0';
    CHECK(mybot_captions_apply_json(captions, json, length) == -1);
}

static void test_invalid(mybot_captions_t *captions) {
    static const char *invalid[] = {
        "",
        "{",
        "[]",
        "null",
        "{}",
        "{\"object\":null}",
        "{\"object\":\"user.transcription\",\"stream_id\":1,\"text\":\"missing turn\"}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"text\":\"missing stream\"}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1}",
        "{\"object\":\"user.transcription\",\"turn_id\":null,\"stream_id\":1,\"text\":\"x\"}",
        "{\"object\":\"user.transcription\",\"turn_id\":true,\"stream_id\":1,\"text\":\"x\"}",
        "{\"object\":\"user.transcription\",\"turn_id\":-1,\"stream_id\":1,\"text\":\"x\"}",
        "{\"object\":\"user.transcription\",\"turn_id\":1.5,\"stream_id\":1,\"text\":\"x\"}",
        "{\"object\":\"user.transcription\",\"turn_id\":\"\",\"stream_id\":1,\"text\":\"x\"}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1,\"text\":true}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1,\"text\":\"x\",\"final\":"
        "1}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1,\"text\":\"x\",\"final\":"
        "null}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1,\"text\":\"x\",\"start_"
        "ms\":-1}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1,\"text\":\"x\",\"start_"
        "ms\":0.5}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1,\"text\":\"x\","
        "\"duration_ms\":\"1\"}",
        "{\"object\":\"user.translation\",\"turn_id\":1,\"stream_id\":1,\"text\":\"x\","
        "\"transcript_text\":1}",
        "{\"object\":\"user.translation\",\"turn_id\":1,\"stream_id\":1,\"text\":\"x\",\"text\":"
        "\"y\"}",
        "{\"object\":\"user.translation\",\"object\":\"user.transcription\"}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1,\"text\":\"\\u0000\"}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1,\"text\":\"\\ud800\"}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1,\"text\":\"\\udc00\"}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1,\"text\":\"\xc0\x80\"}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1,\"text\":"
        "\"\xed\xa0\x80\"}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1,\"text\":"
        "\"\xf4\x90\x80\x80\"}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1,\"text\":\"\xe4\xbd\"}",
        "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1,\"text\":\"x\"} {}",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        expect_invalid(captions, invalid[i]);
    char deep[512];
    strcpy(deep, "{\"object\":\"user.transcription\",\"turn_id\":1,\"stream_id\":1,\"text\":\"x\","
                 "\"unknown\":");
    size_t position = strlen(deep);
    for (unsigned i = 0; i < 16; ++i)
        deep[position++] = '[';
    deep[position++] = '0';
    for (unsigned i = 0; i < 16; ++i)
        deep[position++] = ']';
    deep[position++] = '}';
    deep[position] = '\0';
    expect_invalid(captions, deep);
    CHECK(apply(captions, "{\"object\":\"agent.status\",\"unknown\":{\"x\":[1,2,3]}}") == 0);
    CHECK(apply(captions,
                "{\"object\":\"user.transcription\",\"stream_id\":9223372036854775807,"
                "\"turn_id\":9007199254740993,\"text\":\"large ID\","
                "\"start_ms\":9223372036854775807,\"duration_ms\":9223372036854775807}") == 1);
    CHECK(apply(captions,
                "{\"object\":\"user.transcription\",\"stream_id\":\"9223372036854775807\","
                "\"turn_id\":\"9007199254740993\",\"text\":\"large ID\"}") == 0);
    CHECK(mybot_captions_apply_json(NULL, "{}", 2) == -1);
    CHECK(mybot_captions_apply_json(captions, NULL, 2) == -1);
}

static void test_allocation_failures(mybot_captions_t *captions) {
    const char *json = "{\"object\":\"user.translation\",\"stream_id\":1,\"turn_id\":2,"
                       "\"text\":\"fault\",\"transcript_text\":\"故障\",\"final\":true}";
    bool reached_success = false;
    for (int failure = 0; failure < 100; ++failure) {
        mybot_captions_reset(captions, 7);
        mybot_caption_state_t before = state(captions);
        fail_after = failure;
        int result = apply(captions, json);
        fail_after = -1;
        CHECK(allocations == 1);
        if (result == 1) {
            reached_success = true;
            CHECK(state(captions).count == 1);
            break;
        }
        CHECK(result == -1);
        mybot_caption_state_t after = state(captions);
        CHECK(!memcmp(&before, &after, sizeof(before)));
    }
    CHECK(reached_success);
}

int main(void) {
    /* The first allocation failure must not leave a partially-owned store. */
    fail_after = 0;
    CHECK(mybot_captions_create() == NULL);
    CHECK(!allocations);
    fail_after = -1;
    mybot_captions_t *captions = mybot_captions_create();
    CHECK(captions && allocations == 1);
    mybot_captions_reset(captions, 1);
    CHECK(state(captions).generation == 1 && state(captions).revision == 1);
    test_pairing(captions);
    test_revisions(captions);
    test_ring(captions);
    test_limits(captions);
    test_invalid(captions);
    test_allocation_failures(captions);
    mybot_captions_destroy(captions);
    mybot_captions_destroy(NULL);
    mybot_captions_reset(NULL, 1);
    mybot_caption_state_t empty;
    mybot_captions_get_state(NULL, &empty);
    CHECK(!empty.count && !empty.revision);
    CHECK(!allocations);
    printf("PASS translation_captions (%u checks)\n", checks);
    return 0;
}
