/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_device_client.h"
#include "mybot_http_client.h"
#include "mybot_json.h"
#include <mybot/mybot_build_config.h>

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char valid_response[] =
    "{\"data\":{\"conversation_id\":\"parent-1\",\"voice_agent_type\":\"v2vt\","
    "\"status\":\"started\",\"data_channel\":\"rtm\","
    "\"expires_at\":\"2026-11-03T03:00:00.000Z\","
    "\"rtc\":{\"app_id\":\"wrong-top-app\",\"channel\":\"wrong-top-channel\","
    "\"uid\":\"wrong-top-uid\",\"token\":\"wrong-top-token\"},"
    "\"translation_session\":{\"mode\":\"listen\",\"routes\":[{"
    "\"direction\":\"listen\",\"input_role\":\"remote_original\","
    "\"output_role\":\"local_translation\",\"source_language\":\"en-US\","
    "\"target_language\":\"zh-CN\",\"tts_enabled\":true,"
    "\"agent_uid\":\"01K7M2Z0F00000000000000002\","
    "\"audio_publisher_uid\":\"01K7M2Z0F00000000000000002\","
    "\"rtc\":{\"app_id\":\"route-app\",\"channel\":\"route-channel\","
    "\"uid\":\"01K7M2Z0F00000000000000001\",\"token\":\"route-token\","
    "\"expires_at\":\"2026-11-03T03:00:00.000Z\"}}]}}}";

/* Synthetic service-shaped response; all identities and credentials are invented. */
static const char service_listen_response[] =
    "{\"data\":{\"conversation_id\":\"conv_rtc_synthetic_demo\",\"mode\":\"device\","
    "\"agent_id\":\"agent_synthetic\",\"device_id\":\"synthetic-device\","
    "\"channel\":\"translation-synthetic-channel\","
    "\"local_uid\":\"01K7M2Z0F00000000000000001\","
    "\"agent_uid\":\"01K7M2Z0F00000000000000002\",\"status\":\"started\","
    "\"started_at\":\"2026-11-03T00:00:00.000Z\",\"data_channel\":\"rtm\","
    "\"expires_at\":\"2026-11-03T03:00:00.672Z\","
    "\"rtc\":{\"token\":\"synthetic-listen-token\",\"app_id\":\"synthetic-app\","
    "\"channel\":\"translation-synthetic-channel\","
    "\"uid\":\"01K7M2Z0F00000000000000001\",\"expires_at\":\"2026-11-03T03:00:00.672Z\"},"
    "\"convoai\":{\"session_id\":null,\"status\":null},\"voice_agent_type\":\"v2vt\","
    "\"translation_config_snapshot\":{\"mode\":\"listen\",\"local_language\":\"en-US\","
    "\"remote_language\":\"zh-CN\",\"listen\":{\"keywords\":[],\"terms\":[],\"context\":null,"
    "\"tts_enabled\":true,\"voice_id\":\"synthetic-voice\"},\"speak\":null},"
    "\"translation_session\":{\"mode\":\"listen\",\"routes\":[{\"direction\":\"listen\","
    "\"input_role\":\"remote_original\",\"rtc\":{\"token\":\"synthetic-listen-token\","
    "\"app_id\":\"synthetic-app\",\"channel\":\"translation-synthetic-channel\","
    "\"uid\":\"01K7M2Z0F00000000000000001\",\"expires_at\":\"2026-11-03T03:00:00.672Z\"},"
    "\"agent_uid\":\"01K7M2Z0F00000000000000002\","
    "\"audio_publisher_uid\":\"01K7M2Z0F00000000000000002\","
    "\"source_language\":\"zh-CN\",\"target_language\":\"en-US\",\"tts_enabled\":true,"
    "\"output_role\":\"local_translation\"}]}}}";

/* Both routes use one channel, with four independent identities. */
static const char face_listen_route[] =
    "{\"direction\":\"listen\",\"input_role\":\"remote_original\","
    "\"output_role\":\"local_translation\",\"source_language\":\"en-US\","
    "\"target_language\":\"zh-CN\",\"tts_enabled\":true,"
    "\"agent_uid\":\"agent-listen\",\"audio_publisher_uid\":\"agent-listen\","
    "\"rtc\":{\"app_id\":\"face-app\",\"channel\":\"face-channel\","
    "\"uid\":\"device-listen\",\"token\":\"listen-token\","
    "\"expires_at\":\"2026-11-03T03:00:00.000Z\"}}";
static const char face_speak_route[] =
    "{\"direction\":\"speak\",\"input_role\":\"local_original\","
    "\"output_role\":\"remote_translation\",\"source_language\":\"zh-CN\","
    "\"target_language\":\"en-US\",\"tts_enabled\":true,"
    "\"agent_uid\":\"agent-speak\",\"audio_publisher_uid\":\"agent-speak\","
    "\"rtc\":{\"app_id\":\"face-app\",\"channel\":\"face-channel\","
    "\"uid\":\"device-speak\",\"token\":\"speak-token\","
    "\"expires_at\":\"2026-11-03T03:00:00.000Z\"}}";

static void face_response(char *out, size_t capacity, const char *first, const char *second) {
    int length =
        snprintf(out, capacity,
                 "{\"data\":{\"conversation_id\":\"face-parent\","
                 "\"voice_agent_type\":\"v2vt\",\"status\":\"started\",\"data_channel\":\"rtm\","
                 "\"expires_at\":\"2026-11-03T03:00:00.000Z\","
                 "\"translation_session\":{\"mode\":\"listen_speak\",\"routes\":[%s%s%s]}}}",
                 first, second ? "," : "", second ? second : "");
    assert(length > 0 && (size_t)length < capacity);
}

static int response_status, response_result, http_calls, allocations;
static int request_timeout;
static const char *response_body;
static char request_body[4096];
static time_t wall_time = 1793664000; /* 2026-11-03 00:00:00 UTC */
static bool change_time_on_post;
static time_t post_wall_time;
static bool fail_header_allocation;
static int allocation_calls, fail_allocation_at;
static unsigned checks;
static char diagnostic_log[2048];

void test_client_log(const char *format, ...) {
    size_t offset = strlen(diagnostic_log);
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(diagnostic_log + offset, sizeof(diagnostic_log) - offset, format, arguments);
    va_end(arguments);
}

void *aosl_hal_malloc(size_t size) {
    allocation_calls++;
    if ((fail_allocation_at && allocation_calls == fail_allocation_at) ||
        (fail_header_allocation && size == MYBOT_DEVICE_CLIENT_MAX_TOKEN + 32U)) {
        return NULL;
    }
    void *memory = malloc(size);
    if (memory) {
        allocations++;
    }
    return memory;
}

void aosl_hal_free(void *memory) {
    if (memory) {
        allocations--;
    }
    free(memory);
}

time_t mybot_test_time(time_t *destination) {
    if (destination) {
        *destination = wall_time;
    }
    return wall_time;
}

bool mybot_http_header_value_is_safe(const char *value, bool allow_tab) {
    if (!value || !value[0]) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if ((*p < 32 && !(allow_tab && *p == '\t')) || *p == 127) {
            return false;
        }
    }
    return true;
}

static int respond_to_post(const char *url, const char *content_type, const char *body,
                           const char *headers, int timeout_ms,
                           mybot_http_client_response_t *response) {
    assert(strstr(url, "/devices/device-1/conversations/"));
    assert(strcmp(content_type, "application/json") == 0);
    assert(strcmp(headers, "Authorization: Device device-token\r\n") == 0);
    http_calls++;
    request_timeout = timeout_ms;
    if (change_time_on_post) {
        wall_time = post_wall_time;
    }
    assert(strlen(body) < sizeof(request_body));
    strcpy(request_body, body);
    response->status_code = response_status;
    if (response_body) {
        response->body_len = strlen(response_body);
        response->body = malloc(response->body_len + 1);
        assert(response->body);
        memcpy(response->body, response_body, response->body_len + 1);
    }
    return response_result;
}

int mybot_http_client_post_ex(const char *url, const char *content_type, const char *body,
                              const char *headers, mybot_http_client_response_t *response) {
    return respond_to_post(url, content_type, body, headers, 0, response);
}

int mybot_http_client_post_timeout_ex(const char *url, const char *content_type, const char *body,
                                      const char *headers, int timeout_ms,
                                      mybot_http_client_response_t *response) {
    return respond_to_post(url, content_type, body, headers, timeout_ms, response);
}

int mybot_http_client_get_ex(const char *url, const char *headers,
                             mybot_http_client_response_t *response) {
    (void)url;
    (void)headers;
    (void)response;
    abort();
}

void mybot_http_client_response_free(mybot_http_client_response_t *response) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static void respond(int status, int result, const char *body) {
    assert(allocations == 0);
    response_status = status;
    response_result = result;
    response_body = body;
    http_calls = 0;
    request_timeout = -1;
    allocation_calls = 0;
    change_time_on_post = false;
    request_body[0] = 0;
    diagnostic_log[0] = 0;
}

static int start_languages(mybot_device_conversation_t *session, const char *source,
                           const char *target) {
    int result = mybot_device_client_start_conversation("https://global.invalid/api", "device-1",
                                                        "device-token", MYBOT_TRANSLATION_LISTEN,
                                                        source, target, session);
    assert(allocations == 0);
    if (http_calls) {
        assert(request_timeout == 30000);
    }
    checks++;
    return result;
}

static int start_face(mybot_device_conversation_t *session) {
    int result = mybot_device_client_start_conversation(
        "https://global.invalid/api", "device-1", "device-token", MYBOT_TRANSLATION_LISTEN_SPEAK,
        "zh-CN", "en-US", session);
    assert(allocations == 0);
    if (http_calls)
        assert(request_timeout == 30000);
    checks++;
    return result;
}

static int start(mybot_device_conversation_t *session) {
    return start_languages(session, "en-US", "zh-CN");
}

static void replace_once(const char *original, const char *needle, const char *replacement,
                         char *output, size_t capacity) {
    const char *match = strstr(original, needle);
    assert(match);
    size_t prefix = (size_t)(match - original);
    assert(prefix + strlen(replacement) + strlen(match + strlen(needle)) < capacity);
    memcpy(output, original, prefix);
    strcpy(output + prefix, replacement);
    strcpy(output + prefix + strlen(replacement), match + strlen(needle));
}

static const char *json_string(const mybot_json_t *root, const char *field) {
    const char *value = mybot_json_get_string(mybot_json_get_object_item(root, field));
    assert(value);
    return value;
}

static void assert_request(const char *source, const char *target) {
    mybot_json_t *root = mybot_json_parse(request_body);
    assert(root);
    mybot_json_t *audio = mybot_json_get_object_item(root, "audio");
    mybot_json_t *features = mybot_json_get_object_item(root, "features");
    mybot_json_t *translation = mybot_json_get_object_item(root, "translation");
    int64_t number;
    assert(strcmp(json_string(root, "trigger"), "button") == 0);
    assert(strcmp(json_string(audio, "codec"), "G722") == 0);
    assert(mybot_json_get_integer(mybot_json_get_object_item(audio, "p_time"), &number));
    assert(number == MYBOT_AUDIO_PTIME_MS);
    assert(mybot_json_get_object_item(features, "cloud_aec")->type ==
           (MYBOT_CLOUD_AEC ? MYBOT_JSON_TRUE : MYBOT_JSON_FALSE));
    assert(mybot_json_get_object_item(features, "ai_qos")->type ==
           (MYBOT_AI_QOS ? MYBOT_JSON_TRUE : MYBOT_JSON_FALSE));
#if MYBOT_AI_QOS
    assert(mybot_json_get_integer(mybot_json_get_object_item(features, "fast_send_multiplier"),
                                  &number));
    assert(number == MYBOT_FAST_SEND_MULTIPLIER);
#else
    assert(!mybot_json_get_object_item(features, "fast_send_multiplier"));
#endif
    assert(strcmp(json_string(translation, "mode"), "listen") == 0);
    assert(strcmp(json_string(translation, "remote_language"), source) == 0);
    assert(strcmp(json_string(translation, "local_language"), target) == 0);
    assert(!mybot_json_get_object_item(translation, "tts_enabled"));
    mybot_json_delete(root);
    assert(allocations == 0);
}

static void test_valid_and_request(void) {
    mybot_device_conversation_t session;
    respond(201, 0, valid_response);
    assert(start(&session) == 0);
    assert(http_calls == 1);
    assert(!session.cleanup_required && !session.start_unknown && !session.error_code[0]);
    assert(strcmp(session.conversation_id, "parent-1") == 0);
    assert(strcmp(session.routes[0].rtc_app_id, "route-app") == 0);
    assert(strcmp(session.routes[0].rtc_channel, "route-channel") == 0);
    assert(strcmp(session.routes[0].rtc_token, "route-token") == 0);
    assert(strcmp(session.routes[0].rtc_uid, "01K7M2Z0F00000000000000001") == 0);
    assert(strcmp(session.routes[0].audio_publisher_uid, session.routes[0].rtc_agent_uid) == 0);
    assert(strcmp(session.routes[0].source_language, "en-US") == 0);
    assert(strcmp(session.routes[0].target_language, "zh-CN") == 0);
    assert(session.expires_at == 1793674800);
    assert_request("en-US", "zh-CN");
}

static void test_service_response_and_clock(void) {
    mybot_device_conversation_t session;
    wall_time = 0;
    respond(201, 0, service_listen_response);
    assert(start_languages(&session, "zh-CN", "en-US") < 0);
    assert(strcmp(session.error_code, "TIME_NOT_SYNCED") == 0);
    assert(http_calls == 0 && allocation_calls == 0);
    assert(!session.cleanup_required && !session.start_unknown && !session.conversation_id[0]);
    assert(strstr(diagnostic_log, "local UTC clock not synchronized"));

    /* The same response and language direction work as soon as UTC is available. */
    wall_time = 1793664000;
    respond(201, 0, service_listen_response);
    assert(start_languages(&session, "zh-CN", "en-US") == 0);
    assert(http_calls == 1 && !session.cleanup_required && !session.start_unknown);
    assert(strcmp(session.conversation_id, "conv_rtc_synthetic_demo") == 0);
    assert(strcmp(session.routes[0].rtc_uid, "01K7M2Z0F00000000000000001") == 0);
    assert(strcmp(session.routes[0].audio_publisher_uid, "01K7M2Z0F00000000000000002") == 0);
    assert(session.expires_at == 1793674800);
    assert_request("zh-CN", "en-US");

    /* Losing the local clock during the request still requires cloud cleanup. */
    respond(201, 0, service_listen_response);
    change_time_on_post = true;
    post_wall_time = 0;
    assert(start_languages(&session, "zh-CN", "en-US") < 0);
    assert(http_calls == 1 && session.cleanup_required && !session.start_unknown);
    assert(strcmp(session.error_code, "TIME_NOT_SYNCED") == 0);
    assert(strcmp(session.conversation_id, "conv_rtc_synthetic_demo") == 0);
    assert(strstr(diagnostic_log, "TIME_NOT_SYNCED"));
    assert(strstr(diagnostic_log, "local_utc_unavailable"));
    assert(strstr(diagnostic_log, "http=201"));
    assert(!strstr(diagnostic_log, "synthetic-listen-token"));
    assert(!strstr(diagnostic_log, "device-token"));
    wall_time = 1793664000;
}

static void test_invalid_routes(void) {
    static const struct {
        const char *needle, *replacement;
    } cases[] = {
        {"\"voice_agent_type\":\"v2vt\"", "\"voice_agent_type\":\"convoai\""},
        {"\"data_channel\":\"rtm\"", "\"data_channel\":\"datastream\""},
        {"\"status\":\"started\"", "\"status\":\"starting\""},
        {"\"mode\":\"listen\"", "\"mode\":\"listen_speak\""},
        {"\"routes\":[{", "\"routes\":[{},{"},
        {"\"direction\":\"listen\"", "\"direction\":\"speak\""},
        {"\"input_role\":\"remote_original\"", "\"input_role\":\"local_original\""},
        {"\"output_role\":\"local_translation\"", "\"output_role\":\"remote_translation\""},
        {"\"app_id\":\"route-app\"", "\"app_id\":null"},
        {"\"channel\":\"route-channel\"", "\"channel\":\"\""},
        {"\"token\":\"route-token\"", "\"token\":\"\""},
        {"\"token\":\"route-token\"", "\"token\":\"invalid\\ntoken\""},
        {"\"uid\":\"01K7M2Z0F00000000000000001\"", "\"uid\":11725"},
        {"\"uid\":\"01K7M2Z0F00000000000000001\"", "\"uid\":\"invalid uid\""},
        {"\"uid\":\"01K7M2Z0F00000000000000001\"", "\"uid\":\"01K7M2Z0F00000000000000002\""},
        {"\"agent_uid\":\"01K7M2Z0F00000000000000002\"", "\"agent_uid\":42"},
        {"\"audio_publisher_uid\":\"01K7M2Z0F00000000000000002\"", "\"audio_publisher_uid\":null"},
        {"\"audio_publisher_uid\":\"01K7M2Z0F00000000000000002\"",
         "\"audio_publisher_uid\":\"unknown-publisher\""},
        {"\"source_language\":\"en-US\"", "\"source_language\":\"ja-JP\""},
        {"\"target_language\":\"zh-CN\"", "\"target_language\":\"en-US\""},
        {"\"tts_enabled\":true", "\"tts_enabled\":1"},
        {"\"expires_at\":\"2026-11-03T03:00:00.000Z\"",
         "\"expires_at\":\"2026-02-29T03:00:00.000Z\""},
        {"\"expires_at\":\"2026-11-03T03:00:00.000Z\"", "\"expires_at\":\"2026-11-03T03:00:00\""},
        {"\"expires_at\":\"2026-11-03T03:00:00.000Z\"",
         "\"expires_at\":\"2026-11-03T02:00:00.000Z\""},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char modified[4096];
        replace_once(valid_response, cases[i].needle, cases[i].replacement, modified,
                     sizeof(modified));
        respond(201, 0, modified);
        mybot_device_conversation_t session;
        assert(start(&session) < 0);
        assert(session.cleanup_required && !session.start_unknown);
        assert(strcmp(session.conversation_id, "parent-1") == 0);
        assert(session.error_code[0]);
    }
}

static void test_tts_and_expiry(void) {
    char modified[4096];
    mybot_device_conversation_t session;
    replace_once(valid_response, "\"tts_enabled\":true", "\"tts_enabled\":false", modified,
                 sizeof(modified));
    respond(201, 0, modified);
    assert(start(&session) < 0);
    assert(strcmp(session.error_code, "INVALID_TRANSLATION_ROUTE") == 0);
    char caption_only[4096];
    replace_once(modified, "\"audio_publisher_uid\":\"01K7M2Z0F00000000000000002\"",
                 "\"audio_publisher_uid\":null", caption_only, sizeof(caption_only));
    respond(201, 0, caption_only);
    assert(start(&session) == 0);
    assert(!session.routes[0].tts_enabled && !session.routes[0].audio_publisher_uid[0]);
    assert(!session.cleanup_required && !session.start_unknown);
    respond(201, 0, modified);
    assert(start(&session) < 0);
    assert(session.cleanup_required && !session.start_unknown);
    replace_once(valid_response, "\"expires_at\":\"2026-11-03T03:00:00.000Z\"",
                 "\"expires_at\":\"2026-02-29T03:00:00.000Z\"", modified, sizeof(modified));
    respond(201, 0, modified);
    assert(start(&session) < 0);
    assert(strcmp(session.error_code, "INVALID_TRANSLATION_EXPIRY") == 0);
    assert(session.cleanup_required && !session.start_unknown);
    replace_once(valid_response, "\"expires_at\":\"2026-11-03T03:00:00.000Z\"",
                 "\"expires_at\":\"2026-11-03T04:00:00.000Z\"", modified, sizeof(modified));
    respond(201, 0, modified);
    assert(start(&session) < 0);
    assert(strcmp(session.error_code, "INVALID_TRANSLATION_EXPIRY") == 0);
    assert(session.cleanup_required && !session.start_unknown);
    wall_time = 1793674800;
    respond(201, 0, valid_response);
    assert(start(&session) < 0);
    assert(strcmp(session.error_code, "TRANSLATION_EXPIRED") == 0);
    assert(session.cleanup_required);
    wall_time = 0;
    respond(201, 0, valid_response);
    assert(start(&session) < 0);
    assert(strcmp(session.error_code, "TIME_NOT_SYNCED") == 0);
    assert(!session.cleanup_required && !session.start_unknown && http_calls == 0);
    wall_time = 1793664000;
}

static void test_start_failures(void) {
    static const struct {
        int status, result;
        const char *body;
        bool cleanup, unknown;
        const char *parent;
    } cases[] = {
        {502, 0,
         "{\"error\":{\"code\":\"CONVERSATION_START_FAILED\",\"details\":{"
         "\"cleanup_required\":true,\"conversation_id\":\"cleanup-1\"}}}",
         true, false, "cleanup-1"},
        {403, 0, "{\"error\":{\"code\":\"CONVOAI_SERVICE_NOT_ENABLED\"}}", false, false, ""},
        {502, 0, "{\"error\":{\"details\":{\"cleanup_required\":false}}}", false, false, ""},
        {502, 0, "{\"error\":{\"details\":{\"cleanup_required\":true}}}", true, true, ""},
        {0, -1, NULL, false, true, ""},
        {201, -1, valid_response, true, false, "parent-1"},
        {201, 0, "{\"data\":", false, true, ""},
        {201, 0, "{\"data\":{}}", false, true, ""},
        {200, 0, valid_response, true, false, "parent-1"},
        {202, 0, "{}", false, true, ""},
        {500, 0, "not JSON", false, false, ""},
        {502, -1, "{\"error\":{\"details\":{\"cleanup_required\":false}}}", false, false, ""},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        respond(cases[i].status, cases[i].result, cases[i].body);
        mybot_device_conversation_t session;
        assert(start(&session) != 0);
        assert(session.cleanup_required == cases[i].cleanup);
        assert(session.start_unknown == cases[i].unknown);
        assert(strcmp(session.conversation_id, cases[i].parent) == 0);
        assert(session.error_code[0]);
    }
}

static void test_local_failures(void) {
    mybot_device_conversation_t session;
    respond(201, 0, valid_response);
    assert(mybot_device_client_start_conversation("https://global.invalid/api", "device-1",
                                                  "device-token", MYBOT_TRANSLATION_LISTEN, "en-US",
                                                  "en-US", &session) < 0);
    assert(!session.start_unknown && !session.cleanup_required && http_calls == 0);
    checks++;
    fail_header_allocation = true;
    assert(start(&session) < 0);
    assert(!session.start_unknown && !session.cleanup_required && http_calls == 0);
    assert(strcmp(session.error_code, "OUT_OF_MEMORY") == 0);
    fail_header_allocation = false;
}

static void test_stop(void) {
    static const struct {
        int status, transport;
        const char *body;
        bool success;
    } cases[] = {
        {200, 0, "{\"data\":{\"conversation\":{\"status\":\"ended\"}}}", true},
        {200, 0, "{\"data\":{\"conversation\":{\"status\":\"ending\"}}}", false},
        {200, 0, "{\"data\":{}}", false},
        {200, 0, "not JSON", false},
        {201, 0, "{\"data\":{\"conversation\":{\"status\":\"ended\"}}}", false},
        {204, 0, NULL, false},
        {409, 0, "{}", false},
        {502, 0, "{}", false},
        {503, 0, "{}", false},
        {0, -1, NULL, false},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        respond(cases[i].status, cases[i].transport, cases[i].body);
        int result = mybot_device_client_stop_conversation("https://global.invalid/api", "device-1",
                                                           "device-token", "parent-1", "error");
        assert((result == 0) == cases[i].success);
        assert(request_timeout == 0);
        assert(allocations == 0);
        mybot_json_t *root = mybot_json_parse(request_body);
        assert(strcmp(json_string(root, "conversation_id"), "parent-1") == 0);
        assert(strcmp(json_string(root, "reason"), "error") == 0);
        mybot_json_delete(root);
        assert(allocations == 0);
        checks++;
    }
}

static void test_allocation_failures(void) {
    mybot_device_conversation_t session;
    respond(201, 0, valid_response);
    assert(start(&session) == 0);
    int normal_allocations = allocation_calls;
    for (int i = 1; i <= normal_allocations; i++) {
        respond(201, 0, valid_response);
        fail_allocation_at = i;
        assert(start(&session) < 0);
        fail_allocation_at = 0;
        if (!http_calls) {
            assert(!session.cleanup_required && !session.start_unknown);
        } else {
            assert(session.cleanup_required || session.start_unknown);
        }
        assert(session.error_code[0]);
    }
}

static void test_face_routes_and_request(void) {
    char body[4096];
    mybot_device_conversation_t session;
    /* Reversed response order must still normalize listen to slot zero. */
    face_response(body, sizeof(body), face_speak_route, face_listen_route);
    respond(201, 0, body);
    assert(start_face(&session) == 0);
    assert(session.mode == MYBOT_TRANSLATION_LISTEN_SPEAK && session.route_count == 2);
    assert(session.routes[0].direction == MYBOT_TRANSLATION_DIRECTION_LISTEN);
    assert(session.routes[1].direction == MYBOT_TRANSLATION_DIRECTION_SPEAK);
    assert(strcmp(session.routes[0].rtc_uid, "device-listen") == 0);
    assert(strcmp(session.routes[1].rtc_uid, "device-speak") == 0);
    assert(strcmp(session.routes[0].rtc_token, "listen-token") == 0);
    assert(strcmp(session.routes[1].rtc_token, "speak-token") == 0);
    assert(strcmp(session.routes[0].source_language, "en-US") == 0);
    assert(strcmp(session.routes[1].source_language, "zh-CN") == 0);
    mybot_json_t *root = mybot_json_parse(request_body);
    mybot_json_t *translation = mybot_json_get_object_item(root, "translation");
    assert(strcmp(json_string(translation, "mode"), "listen_speak") == 0);
    assert(strcmp(json_string(translation, "local_language"), "zh-CN") == 0);
    assert(strcmp(json_string(translation, "remote_language"), "en-US") == 0);
    mybot_json_delete(root);
    assert(allocations == 0);
    char listen[2048], caption_only[2048];
    replace_once(face_listen_route, "\"tts_enabled\":true", "\"tts_enabled\":false", listen,
                 sizeof(listen));
    replace_once(listen, "\"audio_publisher_uid\":\"agent-listen\"", "\"audio_publisher_uid\":null",
                 caption_only, sizeof(caption_only));
    face_response(body, sizeof(body), caption_only, face_speak_route);
    respond(201, 0, body);
    assert(start_face(&session) == 0);
    assert(!session.routes[0].tts_enabled && session.routes[1].tts_enabled);
    assert(!session.routes[0].audio_publisher_uid[0]);
}

static void test_face_route_rejections(void) {
    static const struct {
        const char *needle;
        const char *replacement;
    } cases[] = {
        {"\"direction\":\"speak\"", "\"direction\":\"listen\""},
        {"\"direction\":\"speak\"", "\"direction\":\"other\""},
        {"\"input_role\":\"local_original\"", "\"input_role\":\"remote_original\""},
        {"\"output_role\":\"remote_translation\"", "\"output_role\":\"local_translation\""},
        {"\"app_id\":\"face-app\"", "\"app_id\":\"other-app\""},
        {"\"channel\":\"face-channel\"", "\"channel\":\"other-channel\""},
        {"\"uid\":\"device-speak\"", "\"uid\":\"device-listen\""},
        {"\"uid\":\"device-speak\"", "\"uid\":\"agent-listen\""},
        {"\"uid\":\"device-speak\"", "\"uid\":\"agent-speak\""},
        {"\"agent_uid\":\"agent-speak\"", "\"agent_uid\":\"device-listen\""},
        {"\"agent_uid\":\"agent-speak\"", "\"agent_uid\":\"agent-listen\""},
        {"\"source_language\":\"zh-CN\"", "\"source_language\":\"en-US\""},
        {"\"target_language\":\"en-US\"", "\"target_language\":\"zh-CN\""},
        {"\"tts_enabled\":true", "\"tts_enabled\":false"},
        {"\"audio_publisher_uid\":\"agent-speak\"", "\"audio_publisher_uid\":null"},
        {"\"token\":\"speak-token\"", "\"token\":null"},
        {"\"expires_at\":\"2026-11-03T03:00:00.000Z\"",
         "\"expires_at\":\"2026-11-03T02:00:00.000Z\""},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char speak[2048], body[4096];
        replace_once(face_speak_route, cases[i].needle, cases[i].replacement, speak, sizeof(speak));
        face_response(body, sizeof(body), face_listen_route, speak);
        respond(201, 0, body);
        mybot_device_conversation_t session;
        assert(start_face(&session) < 0);
        assert(session.cleanup_required && !session.start_unknown);
        assert(strcmp(session.conversation_id, "face-parent") == 0);
        assert(session.error_code[0] && session.route_count == 0);
    }
    const char *first[] = {face_listen_route, face_speak_route, face_listen_route};
    const char *second[] = {NULL, NULL, face_listen_route};
    for (unsigned i = 0; i < 3; ++i) {
        char body[4096];
        face_response(body, sizeof(body), first[i], second[i]);
        respond(201, 0, body);
        mybot_device_conversation_t session;
        assert(start_face(&session) < 0 && session.cleanup_required);
    }
    mybot_device_conversation_t session;
    respond(201, 0, valid_response);
    assert(start_face(&session) < 0 && session.cleanup_required);
    respond(201, 0, valid_response);
    assert(mybot_device_client_start_conversation("https://global.invalid/api", "device-1",
                                                  "device-token", (mybot_translation_mode_t)99,
                                                  "zh-CN", "en-US", &session) < 0);
    assert(http_calls == 0 && strcmp(session.error_code, "INVALID_TRANSLATION_MODE") == 0);
}

static void test_face_allocation_failures(void) {
    char body[4096];
    face_response(body, sizeof(body), face_listen_route, face_speak_route);
    respond(201, 0, body);
    mybot_device_conversation_t session;
    assert(start_face(&session) == 0);
    int count = allocation_calls;
    for (int i = 1; i <= count; ++i) {
        respond(201, 0, body);
        fail_allocation_at = i;
        assert(start_face(&session) < 0);
        fail_allocation_at = 0;
        if (http_calls)
            assert(session.cleanup_required || session.start_unknown);
        else
            assert(!session.cleanup_required && !session.start_unknown);
    }
}

int main(void) {
    test_valid_and_request();
    test_face_routes_and_request();
    test_face_route_rejections();
    test_face_allocation_failures();
    test_service_response_and_clock();
    test_invalid_routes();
    test_tts_and_expiry();
    test_start_failures();
    test_local_failures();
    test_allocation_failures();
    test_stop();
    printf("translation client: %u protocol/fault cases passed with real JSON parsing\n", checks);
    return 0;
}
