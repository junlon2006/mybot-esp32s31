/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_device_lifecycle.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define SERVER "https://global.example.invalid/api"
#define DEVICE "TEST-DEVICE"

static time_t host_now = 1800000000;
static int allocation_fail_at = -1;
static int allocations;
static int live_allocations;
static int synchronous_stop_result;
static unsigned synchronous_stops;
static char synchronous_server[640], synchronous_device[128], synchronous_credential[512];
static unsigned pair_requests;
static int pair_result;

void *aosl_hal_malloc(size_t size) {
    if (allocation_fail_at >= 0 && allocations++ == allocation_fail_at)
        return NULL;
    void *value = calloc(1, size);
    if (value)
        ++live_allocations;
    return value;
}
void aosl_hal_free(void *value) {
    if (value)
        --live_allocations;
    free(value);
}
time_t time(time_t *destination) {
    if (destination)
        *destination = host_now;
    return host_now;
}

int mybot_device_client_create_pair_code(const char *base, const char *id, const char *firmware,
                                         const char *hardware, mybot_device_pair_code_t *response) {
    (void)base;
    (void)id;
    (void)firmware;
    (void)hardware;
    ++pair_requests;
    snprintf(response->code, sizeof(response->code), "123456");
    snprintf(response->pair_token, sizeof(response->pair_token), "test-pair-token");
    response->poll_after_seconds = 3;
    return pair_result;
}
int mybot_device_client_stop_conversation(const char *base, const char *id, const char *credential,
                                          const char *parent, const char *reason) {
    assert(parent[0] && reason[0]);
    ++synchronous_stops;
    snprintf(synchronous_server, sizeof(synchronous_server), "%s", base);
    snprintf(synchronous_device, sizeof(synchronous_device), "%s", id);
    snprintf(synchronous_credential, sizeof(synchronous_credential), "%s", credential);
    return synchronous_stop_result;
}

typedef struct {
    char key[20];
    unsigned char data[2048];
    size_t size;
    bool present;
} slot_t;

typedef struct {
    mybot_device_lifecycle_t lifecycle;
    mybot_kv_store_t store;
    slot_t slots[4];
    char fail_set[20];
    char fail_get[20];
    char fail_erase[20];
    /* Entries left for the journal after other namespaces; updates coexist
     * with the old blob until the replacement commits. Zero means unlimited. */
    size_t journal_entries_budget;
    size_t journal_peak_entries;
    unsigned journal_budget_failures;
    bool reject_submit;
    bool callback_ends_session;
    unsigned configurations;
    char configured_source[33];
    char configured_target[33];
    unsigned starts;
    unsigned stops;
    unsigned pair_codes;
    unsigned transitions;
    mybot_device_state_t published_state;
    char published_error[64];
    bool published_pending;
    mybot_device_service_request_t *queued[64];
    size_t queued_count;
} fixture_t;

static slot_t *slot(fixture_t *fixture, const char *key, bool create) {
    for (size_t i = 0; i < ARRAY_COUNT(fixture->slots); ++i)
        if (strcmp(fixture->slots[i].key, key) == 0)
            return &fixture->slots[i];
    if (create)
        for (size_t i = 0; i < ARRAY_COUNT(fixture->slots); ++i)
            if (!fixture->slots[i].key[0]) {
                snprintf(fixture->slots[i].key, sizeof(fixture->slots[i].key), "%s", key);
                return &fixture->slots[i];
            }
    return NULL;
}
int mybot_kv_store_get(mybot_kv_store_t *store, const char *key, void *value, size_t capacity,
                       size_t *length) {
    fixture_t *fixture = store->ctx;
    if (strcmp(fixture->fail_get, key) == 0)
        return -1;
    slot_t *entry = slot(fixture, key, false);
    if (!entry || !entry->present)
        return MYBOT_ERR_NOT_FOUND;
    if (entry->size > capacity)
        return -1;
    memcpy(value, entry->data, entry->size);
    *length = entry->size;
    return 0;
}
static size_t journal_entries(size_t bytes) {
    /* 32-byte NVS entries, two chunk headers plus the blob index. This includes
     * the observed page split: the old 1456-byte journal occupied 49 entries. */
    return (bytes + 31U) / 32U + 3U;
}
int mybot_kv_store_set(mybot_kv_store_t *store, const char *key, const void *value, size_t length) {
    fixture_t *fixture = store->ctx;
    if (strcmp(fixture->fail_set, key) == 0)
        return -1;
    slot_t *entry = slot(fixture, key, true);
    assert(entry && length <= sizeof(entry->data));
    if (fixture->journal_entries_budget && strcmp(key, "tr_session") == 0) {
        size_t concurrent =
            journal_entries(length) + (entry->present ? journal_entries(entry->size) : 0);
        if (concurrent > fixture->journal_entries_budget) {
            ++fixture->journal_budget_failures;
            return -1;
        }
        if (concurrent > fixture->journal_peak_entries)
            fixture->journal_peak_entries = concurrent;
    }
    memcpy(entry->data, value, length);
    entry->size = length;
    entry->present = true;
    return 0;
}
int mybot_kv_store_erase(mybot_kv_store_t *store, const char *key) {
    fixture_t *fixture = store->ctx;
    if (strcmp(fixture->fail_erase, key) == 0)
        return -1;
    slot_t *entry = slot(fixture, key, false);
    if (entry)
        entry->present = false;
    return 0;
}
static bool stored(fixture_t *fixture, const char *key) {
    slot_t *entry = slot(fixture, key, false);
    return entry && entry->present;
}
static bool stored_text(fixture_t *fixture, const char *key, const char *text) {
    slot_t *entry = slot(fixture, key, false);
    if (!entry || !entry->present)
        return false;
    for (size_t i = 0; i + strlen(text) <= entry->size; ++i)
        if (memcmp(entry->data + i, text, strlen(text)) == 0)
            return true;
    return false;
}
static void on_pair(const char *code, void *opaque) {
    fixture_t *fixture = opaque;
    assert(strcmp(code, "123456") == 0);
    ++fixture->pair_codes;
}
static void on_start(const mybot_device_conversation_t *response, void *opaque) {
    fixture_t *fixture = opaque;
    assert(stored_text(fixture, "tr_session", response->conversation_id));
    assert(fixture->lifecycle.state == MYBOT_DEVICE_STATE_IN_CONVERSATION);
    assert(!fixture->lifecycle.start_pending);
    ++fixture->starts;
}
static void on_stop(void *opaque) {
    fixture_t *fixture = opaque;
    assert(!fixture->lifecycle.local_media_active);
    assert(stored(fixture, "tr_session"));
    ++fixture->stops;
    if (fixture->callback_ends_session)
        mybot_device_lifecycle_notify_conversation_ended(&fixture->lifecycle);
}
static void on_state(mybot_device_state_t value, void *opaque) {
    fixture_t *fixture = opaque;
    assert(fixture->lifecycle.state == value);
    fixture->published_state = value;
    snprintf(fixture->published_error, sizeof(fixture->published_error), "%s",
             fixture->lifecycle.error_code);
    fixture->published_pending = fixture->lifecycle.journal_present ||
                                 fixture->lifecycle.cleanup_needed ||
                                 fixture->lifecycle.pending_unknown;
    ++fixture->transitions;
}
static void on_configuration(const char *source, const char *target, void *opaque) {
    fixture_t *fixture = opaque;
    assert(source[0] && target[0] && strcmp(source, target) != 0);
    if (fixture->configurations > 0 &&
        (strcmp(source, fixture->configured_source) || strcmp(target, fixture->configured_target)))
        assert(stored(fixture, "tr_languages"));
    snprintf(fixture->configured_source, sizeof(fixture->configured_source), "%s", source);
    snprintf(fixture->configured_target, sizeof(fixture->configured_target), "%s", target);
    ++fixture->configurations;
}
static int submit(mybot_device_service_request_t *request, void *opaque) {
    fixture_t *fixture = opaque;
    if (fixture->reject_submit)
        return -1;
    if (request->type == MYBOT_DEVICE_SERVICE_START)
        assert(stored(fixture, "tr_session"));
    assert(fixture->queued_count < ARRAY_COUNT(fixture->queued));
    fixture->queued[fixture->queued_count++] = request;
    return 0;
}
static void ticks(fixture_t *fixture, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        mybot_device_lifecycle_tick(&fixture->lifecycle);
}
static mybot_device_service_request_t *take(fixture_t *fixture,
                                            mybot_device_service_request_type_t type) {
    for (size_t i = 0; i < fixture->queued_count; ++i)
        if (fixture->queued[i]->type == type) {
            mybot_device_service_request_t *request = fixture->queued[i];
            memmove(&fixture->queued[i], &fixture->queued[i + 1],
                    (fixture->queued_count - i - 1) * sizeof(fixture->queued[0]));
            --fixture->queued_count;
            return request;
        }
    return NULL;
}
static void deliver(fixture_t *fixture, mybot_device_service_request_t *request) {
    assert(request);
    mybot_device_lifecycle_service_result(&fixture->lifecycle, request);
    aosl_hal_free(request);
}
static void initialize(fixture_t *fixture, bool preserve, const char *base, const char *device) {
    if (!preserve)
        memset(fixture, 0, sizeof(*fixture));
    else {
        assert(fixture->queued_count == 0);
        fixture->configurations = 0;
    }
    fixture->store.ctx = fixture;
    mybot_device_lifecycle_callbacks_t callbacks = {
        .on_pair_code = on_pair,
        .on_conversation_start = on_start,
        .on_conversation_stop = on_stop,
        .on_state_changed = on_state,
        .on_configuration_changed = on_configuration,
        .submit_service_request = submit,
        .user_data = fixture,
    };
    assert(mybot_device_lifecycle_init(&fixture->lifecycle, &fixture->store, base, device, "test",
                                       "test-board", "en-US", "zh-CN", &callbacks) == 0);
}
static void bound(fixture_t *fixture) {
    initialize(fixture, false, SERVER, DEVICE);
    ticks(fixture, 1);
    assert(fixture->pair_codes == 1);
    ticks(fixture, 30);
    mybot_device_service_request_t *request = take(fixture, MYBOT_DEVICE_SERVICE_PAIR_POLL);
    assert(request);
    snprintf(request->payload.binding.status, sizeof(request->payload.binding.status), "bound");
    snprintf(request->payload.binding.device_token, sizeof(request->payload.binding.device_token),
             "original-test-credential");
    request->payload.binding.poll_after_seconds = 30;
    deliver(fixture, request);
    assert(fixture->lifecycle.state == MYBOT_DEVICE_STATE_RUNTIME);
    assert(stored(fixture, "device_auth"));
}
static mybot_device_service_request_t *starting(fixture_t *fixture) {
    mybot_device_lifecycle_request_start(&fixture->lifecycle);
    ticks(fixture, 1);
    mybot_device_service_request_t *request = take(fixture, MYBOT_DEVICE_SERVICE_START);
    assert(request && fixture->lifecycle.start_pending);
    assert(fixture->lifecycle.state == MYBOT_DEVICE_STATE_STARTING);
    return request;
}
static void success_response(mybot_device_service_request_t *request, unsigned number) {
    mybot_device_conversation_t *response = &request->payload.start.response;
    snprintf(response->conversation_id, sizeof(response->conversation_id), "parent-%u", number);
    response->mode = request->payload.start.mode;
    response->route_count = response->mode == MYBOT_TRANSLATION_LISTEN_SPEAK ? 2 : 1;
    for (unsigned i = 0; i < response->route_count; ++i) {
        mybot_device_translation_route_t *route = &response->routes[i];
        route->direction =
            i ? MYBOT_TRANSLATION_DIRECTION_SPEAK : MYBOT_TRANSLATION_DIRECTION_LISTEN;
        snprintf(route->rtc_app_id, sizeof(route->rtc_app_id), "test-app");
        snprintf(route->rtc_channel, sizeof(route->rtc_channel), "test-channel");
        snprintf(route->rtc_uid, sizeof(route->rtc_uid), "TEST-LOCAL-%u", i);
        snprintf(route->rtc_agent_uid, sizeof(route->rtc_agent_uid), "TEST-AGENT-%u", i);
        snprintf(route->audio_publisher_uid, sizeof(route->audio_publisher_uid), "TEST-AGENT-%u",
                 i);
        bool reverse = !i && response->mode == MYBOT_TRANSLATION_LISTEN_SPEAK;
        snprintf(route->source_language, sizeof(route->source_language), "%s",
                 reverse ? request->payload.start.target_language
                         : request->payload.start.source_language);
        snprintf(route->target_language, sizeof(route->target_language), "%s",
                 reverse ? request->payload.start.source_language
                         : request->payload.start.target_language);
        route->tts_enabled = true;
        route->expires_at = host_now + 3600;
    }
    response->expires_at = host_now + 3600;
}
static void active(fixture_t *fixture) {
    bound(fixture);
    mybot_device_service_request_t *request = starting(fixture);
    success_response(request, 1);
    deliver(fixture, request);
    assert(fixture->starts == 1 && fixture->lifecycle.local_media_active);
}
static mybot_device_service_request_t *stopping(fixture_t *fixture) {
    mybot_device_lifecycle_request_stop(&fixture->lifecycle);
    ticks(fixture, 1);
    mybot_device_service_request_t *request = take(fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request);
    assert(stored(fixture, "tr_session"));
    return request;
}
static void dispose(fixture_t *fixture) {
    for (size_t i = 0; i < fixture->queued_count; ++i)
        aosl_hal_free(fixture->queued[i]);
    fixture->queued_count = 0;
    assert(live_allocations == 0);
}

static void assert_completed_stop(fixture_t *fixture, const char *error_code) {
    assert(fixture->lifecycle.state == MYBOT_DEVICE_STATE_RUNTIME);
    assert(!fixture->lifecycle.journal_present && !fixture->lifecycle.cleanup_needed);
    assert(!fixture->lifecycle.pending_unknown && !fixture->lifecycle.conversation_id[0]);
    assert(!fixture->lifecycle.local_media_active && !fixture->lifecycle.stop_pending);
    assert(strcmp(fixture->lifecycle.error_code, error_code) == 0);
    /* Consumers must receive the resolved error together with the idle transition. */
    assert(fixture->published_state == MYBOT_DEVICE_STATE_RUNTIME);
    assert(strcmp(fixture->published_error, error_code) == 0);
    assert(!fixture->published_pending);
    assert(!stored(fixture, "tr_session"));
}

static void successful_stop_clears_cleanup_status(void) {
    const int first_results[] = {0, 503, 401};
    for (size_t i = 0; i < ARRAY_COUNT(first_results); ++i) {
        fixture_t fixture;
        active(&fixture);
        mybot_device_service_request_t *request = stopping(&fixture);
        assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_STOPPING);
        assert(strcmp(fixture.lifecycle.error_code, "CLEANUP_PENDING") == 0);
        request->result = first_results[i];
        deliver(&fixture, request);
        if (first_results[i] != 0) {
            assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_CLEANUP_PENDING);
            assert(strcmp(fixture.lifecycle.error_code,
                          first_results[i] == 401 ? "CLEANUP_AUTH_REJECTED" : "CLEANUP_PENDING") ==
                   0);
            assert(stored(&fixture, "tr_session"));
            ticks(&fixture, 10);
            request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
            assert(request);
            deliver(&fixture, request);
        }
        assert_completed_stop(&fixture, "");
        dispose(&fixture);
    }
}

static void repeated_start_stop(void) {
    fixture_t fixture;
    bound(&fixture);
    fixture.callback_ends_session = true;
    for (unsigned i = 0; i < 6; ++i) {
        mybot_device_service_request_t *request = starting(&fixture);
        success_response(request, i);
        deliver(&fixture, request);
        mybot_device_lifecycle_request_start(&fixture.lifecycle);
        ticks(&fixture, 1);
        assert(fixture.queued_count == 0);
        request = stopping(&fixture);
        assert(fixture.stops == i + 1);
        mybot_device_lifecycle_request_start(&fixture.lifecycle);
        ticks(&fixture, 1);
        assert(fixture.queued_count == 0);
        deliver(&fixture, request);
        assert_completed_stop(&fixture, "");
    }
    assert(fixture.starts == 6 && fixture.stops == 6);
    dispose(&fixture);
}
static void cancelled_late_start(void) {
    fixture_t fixture;
    bound(&fixture);
    mybot_device_service_request_t *request = starting(&fixture);
    mybot_device_lifecycle_request_stop(&fixture.lifecycle);
    /* Return before the next control tick: cancellation must already gate media. */
    success_response(request, 12);
    deliver(&fixture, request);
    assert(fixture.starts == 0);
    assert(stored_text(&fixture, "tr_session", "parent-12"));
    ticks(&fixture, 1);
    request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request);
    deliver(&fixture, request);
    assert_completed_stop(&fixture, "");
    dispose(&fixture);
}
static void disconnected_late_start(void) {
    fixture_t fixture;
    bound(&fixture);
    mybot_device_service_request_t *request = starting(&fixture);
    mybot_device_lifecycle_set_network_available(&fixture.lifecycle, false);
    mybot_device_lifecycle_set_network_available(&fixture.lifecycle, true);
    success_response(request, 13);
    deliver(&fixture, request);
    assert(fixture.starts == 0 && fixture.lifecycle.conversation_id[0]);
    ticks(&fixture, 1);
    request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request);
    mybot_device_lifecycle_set_network_available(&fixture.lifecycle, false);
    deliver(&fixture, request); /* Confirmed ended must apply across network generations. */
    assert(!stored(&fixture, "tr_session"));
    dispose(&fixture);
}
static void unknown_start_persists(void) {
    fixture_t fixture;
    bound(&fixture);
    mybot_device_service_request_t *request = starting(&fixture);
    request->result = -1;
    request->payload.start.response.start_unknown = true;
    deliver(&fixture, request);
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_START_UNKNOWN);
    mybot_device_lifecycle_request_start(&fixture.lifecycle);
    mybot_device_lifecycle_request_pair(&fixture.lifecycle);
    ticks(&fixture, 100);
    assert(fixture.queued_count == 0 && stored(&fixture, "tr_session"));
    initialize(&fixture, true, SERVER, DEVICE);
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_START_UNKNOWN);
    mybot_device_lifecycle_request_start(&fixture.lifecycle);
    ticks(&fixture, 100);
    assert(fixture.queued_count == 0);
    dispose(&fixture);
}
static void unknown_crash_before_response(void) {
    fixture_t fixture;
    bound(&fixture);
    mybot_device_service_request_t *request = starting(&fixture);
    aosl_hal_free(request); /* Simulated reset discards RAM, not the pre-dispatch journal. */
    initialize(&fixture, true, SERVER, DEVICE);
    assert(fixture.lifecycle.pending_unknown);
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_START_UNKNOWN);
    dispose(&fixture);
}
static void restart_uses_original_identity(void) {
    fixture_t fixture;
    active(&fixture);
    unsigned pairs = pair_requests;
    initialize(&fixture, true, "https://different.example.invalid/api", "OTHER-DEVICE");
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_CLEANUP_PENDING);
    ticks(&fixture, 1);
    mybot_device_service_request_t *request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request);
    assert(strcmp(request->server_base, SERVER) == 0);
    assert(strcmp(request->device_id, DEVICE) == 0);
    assert(strcmp(request->credential, "original-test-credential") == 0);
    assert(pair_requests == pairs);
    request->result = 503;
    deliver(&fixture, request);
    assert(stored(&fixture, "tr_session"));
    ticks(&fixture, 10);
    request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request);
    deliver(&fixture, request);
    ticks(&fixture, 1);
    assert(pair_requests == pairs + 1);
    dispose(&fixture);
}
static void failed_stop_backoff(void) {
    fixture_t fixture;
    active(&fixture);
    mybot_device_service_request_t *request = stopping(&fixture);
    const unsigned delay[] = {10, 20, 40, 80, 160, 300, 300};
    for (size_t i = 0; i < ARRAY_COUNT(delay); ++i) {
        request->result = i == 0 ? 409 : (i == 1 ? 401 : 502);
        deliver(&fixture, request);
        assert(fixture.lifecycle.stop_retry_ticks_remaining == (int)delay[i]);
        assert(stored(&fixture, "tr_session"));
        mybot_device_lifecycle_request_pair(&fixture.lifecycle);
        mybot_device_lifecycle_request_start(&fixture.lifecycle);
        ticks(&fixture, delay[i] - 1);
        assert(fixture.queued_count == 0);
        ticks(&fixture, 1);
        request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
        assert(request);
    }
    request->result = 0;
    deliver(&fixture, request);
    assert_completed_stop(&fixture, "");
    dispose(&fixture);
}
static void failed_start_with_cleanup(void) {
    fixture_t fixture;
    bound(&fixture);
    mybot_device_service_request_t *request = starting(&fixture);
    success_response(request, 19);
    request->result = 502;
    request->payload.start.response.cleanup_required = true;
    snprintf(request->payload.start.response.error_code,
             sizeof(request->payload.start.response.error_code), "CONVERSATION_START_FAILED");
    deliver(&fixture, request);
    assert(fixture.starts == 0 && fixture.lifecycle.cleanup_needed);
    assert(stored_text(&fixture, "tr_session", "parent-19"));
    ticks(&fixture, 1);
    request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request);
    request->result = 503;
    deliver(&fixture, request);
    assert(strcmp(fixture.lifecycle.error_code, "CONVERSATION_START_FAILED") == 0);
    ticks(&fixture, 10);
    request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request);
    deliver(&fixture, request);
    assert_completed_stop(&fixture, "CONVERSATION_START_FAILED");
    dispose(&fixture);
}
static void definite_rejection_allows_retry(void) {
    fixture_t fixture;
    bound(&fixture);
    mybot_device_service_request_t *request = starting(&fixture);
    request->result = 400;
    snprintf(request->payload.start.response.error_code,
             sizeof(request->payload.start.response.error_code), "VALIDATION_ERROR");
    deliver(&fixture, request);
    assert(!stored(&fixture, "tr_session"));
    assert(strcmp(fixture.lifecycle.error_code, "VALIDATION_ERROR") == 0);
    request = starting(&fixture);
    request->result = 401;
    deliver(&fixture, request);
    assert(fixture.lifecycle.pairing_requested && !stored(&fixture, "device_auth"));
    dispose(&fixture);
}
static void storage_failure_before_start(void) {
    fixture_t fixture;
    bound(&fixture);
    snprintf(fixture.fail_set, sizeof(fixture.fail_set), "tr_session");
    mybot_device_lifecycle_request_start(&fixture.lifecycle);
    ticks(&fixture, 1);
    assert(fixture.queued_count == 0 && fixture.starts == 0);
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_ERROR);
    fixture.fail_set[0] = '\0';
    mybot_device_lifecycle_request_start(&fixture.lifecycle);
    ticks(&fixture, 1);
    assert(fixture.queued_count == 0); /* Failure is latched for this runtime. */
    dispose(&fixture);
}
static void storage_failure_after_start(void) {
    fixture_t fixture;
    bound(&fixture);
    mybot_device_service_request_t *request = starting(&fixture);
    success_response(request, 22);
    snprintf(fixture.fail_set, sizeof(fixture.fail_set), "tr_session");
    deliver(&fixture, request);
    assert(fixture.starts == 0 && fixture.lifecycle.storage_fault);
    assert(fixture.lifecycle.conversation_id[0]);
    ticks(&fixture, 1);
    request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request);
    deliver(&fixture, request);
    assert(!stored(&fixture, "tr_session"));
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_ERROR);
    assert(strcmp(fixture.lifecycle.error_code, "STORAGE_ERROR") == 0);
    assert(fixture.published_state == MYBOT_DEVICE_STATE_ERROR);
    assert(strcmp(fixture.published_error, "STORAGE_ERROR") == 0);
    mybot_device_lifecycle_request_start(&fixture.lifecycle);
    ticks(&fixture, 1);
    assert(fixture.queued_count == 0 && fixture.starts == 0);
    dispose(&fixture);
}
static void storage_erase_failure_retains_id(void) {
    fixture_t fixture;
    active(&fixture);
    mybot_device_service_request_t *request = stopping(&fixture);
    snprintf(fixture.fail_erase, sizeof(fixture.fail_erase), "tr_session");
    deliver(&fixture, request);
    assert(stored(&fixture, "tr_session") && fixture.lifecycle.conversation_id[0]);
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_ERROR);
    fixture.fail_erase[0] = '\0';
    ticks(&fixture, 10);
    request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request);
    deliver(&fixture, request);
    assert(!stored(&fixture, "tr_session"));
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_ERROR);
    assert(strcmp(fixture.lifecycle.error_code, "STORAGE_ERROR") == 0);
    assert(fixture.published_state == MYBOT_DEVICE_STATE_ERROR);
    assert(strcmp(fixture.published_error, "STORAGE_ERROR") == 0);
    mybot_device_lifecycle_request_start(&fixture.lifecycle);
    ticks(&fixture, 1);
    assert(fixture.queued_count == 0);
    dispose(&fixture);
}
static void rejected_submission(void) {
    fixture_t fixture;
    bound(&fixture);
    fixture.reject_submit = true;
    mybot_device_lifecycle_request_start(&fixture.lifecycle);
    ticks(&fixture, 1);
    assert(!fixture.lifecycle.start_pending && !stored(&fixture, "tr_session"));
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_RUNTIME);
    fixture.reject_submit = false;
    mybot_device_service_request_t *request = starting(&fixture);
    success_response(request, 25);
    deliver(&fixture, request);
    fixture.reject_submit = true;
    mybot_device_lifecycle_request_stop(&fixture.lifecycle);
    ticks(&fixture, 1);
    assert(fixture.stops == 1 && stored(&fixture, "tr_session"));
    assert(!fixture.lifecycle.stop_pending);
    dispose(&fixture);
}
static void expiry_and_callback(void) {
    fixture_t fixture;
    active(&fixture);
    fixture.lifecycle.expires_at = host_now;
    ticks(&fixture, 1);
    mybot_device_service_request_t *request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request && strcmp(request->payload.stop.reason, "timeout") == 0);
    deliver(&fixture, request);
    request = starting(&fixture);
    success_response(request, 27);
    deliver(&fixture, request);
    mybot_device_lifecycle_notify_credentials_expiring(&fixture.lifecycle);
    ticks(&fixture, 1);
    request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request && strcmp(request->payload.stop.reason, "timeout") == 0);
    deliver(&fixture, request);
    dispose(&fixture);
}
static void shutdown_late_start(void) {
    fixture_t fixture;
    bound(&fixture);
    mybot_device_service_request_t *request = starting(&fixture);
    mybot_device_lifecycle_shutdown(&fixture.lifecycle);
    success_response(request, 28);
    deliver(&fixture, request); /* Applied after worker join even though shutting_down is true. */
    assert(fixture.starts == 0 && fixture.lifecycle.conversation_id[0]);
    unsigned before = synchronous_stops;
    synchronous_stop_result = 503;
    mybot_device_lifecycle_finish_shutdown(&fixture.lifecycle);
    assert(synchronous_stops == before + 1 && stored(&fixture, "tr_session"));
    initialize(&fixture, true, SERVER, DEVICE);
    ticks(&fixture, 1);
    request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request);
    deliver(&fixture, request);
    assert(!stored(&fixture, "tr_session"));
    synchronous_stop_result = 0;
    dispose(&fixture);
}
static void shutdown_active_offline(void) {
    fixture_t fixture;
    active(&fixture);
    mybot_device_lifecycle_set_network_available(&fixture.lifecycle, false);
    mybot_device_lifecycle_shutdown(&fixture.lifecycle);
    assert(fixture.stops == 1 && fixture.queued_count == 0);
    unsigned before = synchronous_stops;
    mybot_device_lifecycle_finish_shutdown(&fixture.lifecycle);
    assert(synchronous_stops == before && stored(&fixture, "tr_session"));
    dispose(&fixture);
}
static void re_pair_preserves_pending(void) {
    fixture_t fixture;
    bound(&fixture);
    mybot_device_service_request_t *request = starting(&fixture);
    mybot_device_lifecycle_request_pair(&fixture.lifecycle);
    success_response(request, 31);
    deliver(&fixture, request);
    assert(fixture.starts == 0 && stored(&fixture, "tr_session"));
    ticks(&fixture, 1);
    request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request && strcmp(request->credential, "original-test-credential") == 0);
    deliver(&fixture, request);
    ticks(&fixture, 1);
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_AWAITING_CLAIM);
    dispose(&fixture);
}
static void language_persistence(void) {
    fixture_t fixture;
    bound(&fixture);
    assert(mybot_device_lifecycle_cycle_language(&fixture.lifecycle, false) == 0);
    assert(strcmp(fixture.lifecycle.target_language, "ja-JP") == 0); /* Skip source en-US. */
    assert(mybot_device_lifecycle_cycle_language(&fixture.lifecycle, true) == 0);
    assert(strcmp(fixture.lifecycle.source_language, "ko-KR") == 0); /* Skip target ja-JP. */
    initialize(&fixture, true, SERVER, DEVICE);
    assert(strcmp(fixture.lifecycle.source_language, "ko-KR") == 0);
    assert(strcmp(fixture.lifecycle.target_language, "ja-JP") == 0);
    mybot_device_service_request_t *request = starting(&fixture);
    assert(strcmp(request->payload.start.source_language, "ko-KR") == 0);
    assert(mybot_device_lifecycle_cycle_language(&fixture.lifecycle, true) == -1);
    success_response(request, 32);
    deliver(&fixture, request);
    assert(mybot_device_lifecycle_cycle_language(&fixture.lifecycle, true) == -1);
    request = stopping(&fixture);
    deliver(&fixture, request);
    snprintf(fixture.fail_set, sizeof(fixture.fail_set), "tr_languages");
    assert(mybot_device_lifecycle_cycle_language(&fixture.lifecycle, true) == -1);
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_ERROR);
    dispose(&fixture);
}
static void direct_language_selection(void) {
    fixture_t fixture;
    bound(&fixture);
    unsigned configurations = fixture.configurations;
    assert(mybot_device_lifecycle_set_languages(&fixture.lifecycle, "zh-CN", "en-US") == 0);
    assert(fixture.configurations == configurations + 1);
    assert(strcmp(fixture.lifecycle.source_language, "zh-CN") == 0);
    assert(strcmp(fixture.lifecycle.target_language, "en-US") == 0);
    assert(stored(&fixture, "tr_languages"));
    assert(mybot_device_lifecycle_set_languages(&fixture.lifecycle, "zh-CN", "en-US") == 0);
    assert(fixture.configurations == configurations + 1); /* Same pair never rewrites flash. */
    assert(mybot_device_lifecycle_set_languages(&fixture.lifecycle, "en-US", "zh-CN") == 0);
    initialize(&fixture, true, SERVER, DEVICE);
    assert(strcmp(fixture.lifecycle.source_language, "en-US") == 0);
    assert(strcmp(fixture.lifecycle.target_language, "zh-CN") == 0);
    configurations = fixture.configurations;
    assert(mybot_device_lifecycle_set_languages(&fixture.lifecycle, "en-US", "en-US") == -1);
    assert(mybot_device_lifecycle_set_languages(&fixture.lifecycle, "zz-ZZ", "en-US") == -1);
    assert(mybot_device_lifecycle_set_languages(&fixture.lifecycle, NULL, "en-US") == -1);
    assert(fixture.configurations == configurations);
    mybot_device_lifecycle_request_start(&fixture.lifecycle);
    assert(mybot_device_lifecycle_set_languages(&fixture.lifecycle, "ja-JP", "ko-KR") == -1);
    mybot_device_service_request_t *request = starting(&fixture);
    assert(mybot_device_lifecycle_set_languages(&fixture.lifecycle, "ja-JP", "ko-KR") == -1);
    success_response(request, 120);
    deliver(&fixture, request);
    assert(mybot_device_lifecycle_set_languages(&fixture.lifecycle, "ja-JP", "ko-KR") == -1);
    request = stopping(&fixture);
    assert(mybot_device_lifecycle_set_languages(&fixture.lifecycle, "ja-JP", "ko-KR") == -1);
    deliver(&fixture, request);
    snprintf(fixture.fail_set, sizeof(fixture.fail_set), "tr_languages");
    assert(mybot_device_lifecycle_set_languages(&fixture.lifecycle, "ja-JP", "ko-KR") == -1);
    assert(strcmp(fixture.lifecycle.source_language, "en-US") == 0);
    assert(strcmp(fixture.lifecycle.target_language, "zh-CN") == 0);
    assert(fixture.lifecycle.storage_fault && fixture.lifecycle.state == MYBOT_DEVICE_STATE_ERROR);
    initialize(&fixture, true, SERVER, DEVICE);
    assert(strcmp(fixture.lifecycle.source_language, "en-US") == 0);
    assert(strcmp(fixture.lifecycle.target_language, "zh-CN") == 0);
    mybot_device_lifecycle_shutdown(&fixture.lifecycle);
    assert(mybot_device_lifecycle_set_languages(&fixture.lifecycle, "ja-JP", "ko-KR") == -1);
    dispose(&fixture);
}
static void corrupt_and_unreadable_journal(void) {
    fixture_t fixture;
    bound(&fixture);
    unsigned char corrupt[] = {1, 2, 3};
    assert(mybot_kv_store_set(&fixture.store, "tr_session", corrupt, sizeof(corrupt)) == 0);
    initialize(&fixture, true, SERVER, DEVICE);
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_ERROR && stored(&fixture, "tr_session"));
    mybot_device_lifecycle_request_start(&fixture.lifecycle);
    mybot_device_lifecycle_request_pair(&fixture.lifecycle);
    ticks(&fixture, 5);
    assert(fixture.queued_count == 0 && stored(&fixture, "tr_session"));
    snprintf(fixture.fail_get, sizeof(fixture.fail_get), "tr_session");
    initialize(&fixture, true, SERVER, DEVICE);
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_ERROR && stored(&fixture, "tr_session"));
    dispose(&fixture);
}
/* Persistent layouts are deliberately explicit: legacy recovery must not
 * depend on the production encoder silently changing the test fixture. */
typedef struct {
    uint32_t version, phase;
    char server[640], device[128], credential[512], parent[128], reason[32];
    int64_t expires_at;
} legacy_journal_t;
typedef struct {
    uint32_t version, phase;
    int64_t expires_at;
} compact_journal_header_t;
_Static_assert(sizeof(legacy_journal_t) == 1456, "Legacy fixture layout changed");
_Static_assert(sizeof(compact_journal_header_t) == 16, "Compact fixture header changed");

static void legacy_journal_recovery(void) {
    for (unsigned phase = 1; phase <= 2; ++phase) {
        fixture_t fixture;
        bound(&fixture);
        legacy_journal_t legacy = {.version = 1, .phase = phase, .expires_at = host_now + 3600};
        snprintf(legacy.server, sizeof(legacy.server), "%s", SERVER);
        snprintf(legacy.device, sizeof(legacy.device), "%s", DEVICE);
        snprintf(legacy.credential, sizeof(legacy.credential), "legacy-cleanup-credential");
        snprintf(legacy.reason, sizeof(legacy.reason), "device_hangup");
        if (phase == 2)
            snprintf(legacy.parent, sizeof(legacy.parent), "legacy-parent");
        assert(mybot_kv_store_set(&fixture.store, "tr_session", &legacy, sizeof(legacy)) == 0);
        initialize(&fixture, true, "https://rebound.example.invalid/api", "REBOUND-DEVICE");
        assert(fixture.lifecycle.journal_present && fixture.lifecycle.cleanup_needed);
        assert(fixture.lifecycle.expires_at == legacy.expires_at);
        assert(strcmp(fixture.lifecycle.pending_credential, legacy.credential) == 0);
        mybot_device_lifecycle_request_start(&fixture.lifecycle);
        ticks(&fixture, 1);
        assert(!take(&fixture, MYBOT_DEVICE_SERVICE_START));
        if (phase == 1) {
            assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_START_UNKNOWN);
            assert(fixture.queued_count == 0 && fixture.lifecycle.pending_unknown);
        } else {
            mybot_device_service_request_t *request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
            assert(request && strcmp(request->payload.stop.conversation_id, legacy.parent) == 0);
            assert(strcmp(request->credential, legacy.credential) == 0);
            assert(strcmp(request->server_base, SERVER) == 0);
            assert(strcmp(request->device_id, DEVICE) == 0);
            deliver(&fixture, request);
            assert(!stored(&fixture, "tr_session"));
        }
        dispose(&fixture);
    }
}

static void compact_journal_roundtrip(void) {
    for (unsigned phase = 1; phase <= 2; ++phase) {
        fixture_t fixture;
        bound(&fixture);
        mybot_device_service_request_t *request = starting(&fixture);
        if (phase == 2) {
            success_response(request, 62);
            deliver(&fixture, request);
        } else {
            aosl_hal_free(request); /* Reset after dispatch, before a response. */
        }
        slot_t *record = slot(&fixture, "tr_session", false);
        compact_journal_header_t header;
        assert(record && record->size < sizeof(legacy_journal_t));
        memcpy(&header, record->data, sizeof(header));
        assert(header.version == 2 && header.phase == phase);
        initialize(&fixture, true, SERVER, DEVICE);
        assert(fixture.lifecycle.journal_present && fixture.lifecycle.cleanup_needed);
        assert(strcmp(fixture.lifecycle.pending_credential, "original-test-credential") == 0);
        assert(strcmp(fixture.lifecycle.pending_server_base, SERVER) == 0);
        assert(strcmp(fixture.lifecycle.pending_device_id, DEVICE) == 0);
        assert(strcmp(fixture.lifecycle.stop_reason, "device_hangup") == 0);
        mybot_device_lifecycle_request_start(&fixture.lifecycle);
        ticks(&fixture, 1);
        assert(!take(&fixture, MYBOT_DEVICE_SERVICE_START));
        if (phase == 1) {
            assert(fixture.queued_count == 0 && fixture.lifecycle.pending_unknown);
            assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_START_UNKNOWN);
        } else {
            request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
            assert(request && strcmp(request->payload.stop.conversation_id, "parent-62") == 0);
            assert(fixture.lifecycle.expires_at == host_now + 3600);
            deliver(&fixture, request);
            assert(!stored(&fixture, "tr_session"));
        }
        dispose(&fixture);
    }
}

static size_t encode_test_journal(unsigned char *out, uint32_t version, uint32_t phase,
                                  const char *fields[5]) {
    const compact_journal_header_t header = {version, phase, host_now + 3600};
    memcpy(out, &header, sizeof(header));
    size_t length = sizeof(header);
    for (size_t i = 0; i < 5; ++i) {
        size_t bytes = strlen(fields[i]) + 1;
        assert(length + bytes <= 2048);
        memcpy(out + length, fields[i], bytes);
        length += bytes;
    }
    return length;
}
static void reject_journal_unchanged(const void *bytes, size_t length) {
    fixture_t fixture;
    bound(&fixture);
    assert(mybot_kv_store_set(&fixture.store, "tr_session", bytes, length) == 0);
    initialize(&fixture, true, SERVER, DEVICE);
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_ERROR && fixture.lifecycle.storage_fault);
    mybot_device_lifecycle_request_start(&fixture.lifecycle);
    mybot_device_lifecycle_request_pair(&fixture.lifecycle);
    ticks(&fixture, 10);
    assert(fixture.queued_count == 0);
    slot_t *record = slot(&fixture, "tr_session", false);
    assert(record && record->present && record->size == length);
    assert(memcmp(record->data, bytes, length) == 0);
    dispose(&fixture);
}
static void compact_journal_rejects_malformed(void) {
    unsigned char bytes[2048];
    const char *fields[5] = {SERVER, DEVICE, "cleanup-credential", "cleanup-parent", "error"};
    size_t length = encode_test_journal(bytes, 2, 2, fields);
    /* Every truncated prefix, including the final missing NUL, is rejected. */
    for (size_t truncated = 0; truncated < length; ++truncated)
        reject_journal_unchanged(bytes, truncated);
    bytes[length] = 0;
    reject_journal_unchanged(bytes, length + 1);
    length = encode_test_journal(bytes, 3, 2, fields);
    reject_journal_unchanged(bytes, length);
    length = encode_test_journal(bytes, 2, 0, fields);
    reject_journal_unchanged(bytes, length);
    length = encode_test_journal(bytes, 2, 3, fields);
    reject_journal_unchanged(bytes, length);
    for (size_t i = 0; i < 5; ++i) {
        const char *saved = fields[i];
        fields[i] = "";
        length = encode_test_journal(bytes, 2, 2, fields);
        reject_journal_unchanged(bytes, length); /* Known phase requires all five fields. */
        fields[i] = saved;
    }
    const size_t capacities[5] = {640, 128, 512, 128, 32};
    char oversized[641];
    for (size_t i = 0; i < 5; ++i) {
        memset(oversized, 'x', capacities[i]);
        oversized[capacities[i]] = '\0';
        const char *saved = fields[i];
        fields[i] = oversized;
        length = encode_test_journal(bytes, 2, 2, fields);
        reject_journal_unchanged(bytes, length);
        fields[i] = saved;
    }
    length = encode_test_journal(bytes, 2, 2, fields);
    memset(bytes + length, 'x', sizeof(legacy_journal_t) + 1 - length);
    reject_journal_unchanged(bytes, sizeof(legacy_journal_t) + 1);
}

static void compact_journal_fits_existing_nvs(void) {
    fixture_t fixture;
    bound(&fixture);
    fixture.journal_entries_budget = 67; /* Observed available entries before first write. */
    legacy_journal_t old = {0};
    assert(journal_entries(sizeof(old)) == 49);
    assert(mybot_kv_store_set(&fixture.store, "tr_session", &old, sizeof(old)) == 0);
    assert(fixture.journal_entries_budget - journal_entries(sizeof(old)) == 18);
    old.phase = 2;
    assert(mybot_kv_store_set(&fixture.store, "tr_session", &old, sizeof(old)) < 0);
    assert(slot(&fixture, "tr_session", false)->data[4] == 0); /* Old journal survived. */
    assert(mybot_kv_store_erase(&fixture.store, "tr_session") == 0);
    dispose(&fixture);

    bound(&fixture);
    fixture.journal_entries_budget = 67;
    snprintf(fixture.lifecycle.server_base, sizeof(fixture.lifecycle.server_base),
             "https://example.invalid/device/api");
    /* Synthetic endpoint and device ID match the deployed lengths: 34/30. */
    snprintf(fixture.lifecycle.device_id, sizeof(fixture.lifecycle.device_id),
             "ESP32-0123456789ABCDEF01234567");
    memset(fixture.lifecycle.device_token, 'k', sizeof(fixture.lifecycle.device_token) - 1);
    fixture.lifecycle.device_token[sizeof(fixture.lifecycle.device_token) - 1] = '\0';
    for (unsigned i = 0; i < 4; ++i) {
        mybot_device_service_request_t *request = starting(&fixture);
        assert(slot(&fixture, "tr_session", false)->size == 609);
        success_response(request, 63);
        snprintf(request->payload.start.response.conversation_id,
                 sizeof(request->payload.start.response.conversation_id), "conv_rtc_012345678901");
        deliver(&fixture, request);
        assert(fixture.starts == i + 1 && !fixture.lifecycle.storage_fault);
        assert(slot(&fixture, "tr_session", false)->size == 630);
        request = stopping(&fixture);
        deliver(&fixture, request);
        assert_completed_stop(&fixture, "");
    }
    assert(fixture.journal_peak_entries <= 67 && fixture.journal_budget_failures == 0);
    dispose(&fixture);
}

static void duplicate_and_stale_results(void) {
    fixture_t fixture;
    bound(&fixture);
    mybot_device_service_request_t *request = starting(&fixture);
    success_response(request, 34);
    mybot_device_service_request_t duplicate = *request;
    deliver(&fixture, request);
    mybot_device_lifecycle_service_result(&fixture.lifecycle, &duplicate);
    assert(fixture.starts == 1);
    request = stopping(&fixture);
    duplicate = *request;
    deliver(&fixture, request);
    mybot_device_lifecycle_service_result(&fixture.lifecycle, &duplicate);
    assert(fixture.stops == 1 && fixture.lifecycle.state == MYBOT_DEVICE_STATE_RUNTIME);
    request = starting(&fixture);
    success_response(request, 35);
    ++duplicate.request_generation;
    mybot_device_lifecycle_service_result(&fixture.lifecycle, &duplicate);
    assert(fixture.lifecycle.start_pending && stored(&fixture, "tr_session"));
    deliver(&fixture, request);
    assert(fixture.starts == 2);
    dispose(&fixture);
}
static void allocation_failure(void) {
    fixture_t fixture;
    bound(&fixture);
    allocations = 0;
    allocation_fail_at = 0;
    mybot_device_lifecycle_request_start(&fixture.lifecycle);
    ticks(&fixture, 1);
    assert(fixture.queued_count == 0 && !stored(&fixture, "tr_session"));
    allocations = 0;
    allocation_fail_at = 1; /* Request allocated, durable preflight allocation fails. */
    mybot_device_lifecycle_request_start(&fixture.lifecycle);
    ticks(&fixture, 1);
    assert(fixture.queued_count == 0 && fixture.lifecycle.storage_fault);
    allocation_fail_at = -1;
    dispose(&fixture);
}

static void binding_revocation_retains_cleanup_identity(void) {
    fixture_t fixture;
    active(&fixture);
    fixture.lifecycle.runtime_tick_counter = fixture.lifecycle.runtime_poll_interval * 10 - 1;
    ticks(&fixture, 1);
    mybot_device_service_request_t *request = take(&fixture, MYBOT_DEVICE_SERVICE_DEVICE_POLL);
    assert(request);
    request->result = 401;
    deliver(&fixture, request);
    assert(fixture.stops == 1 && !fixture.lifecycle.device_token[0]);
    assert(stored_text(&fixture, "tr_session", "original-test-credential"));
    ticks(&fixture, 1);
    request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request && strcmp(request->credential, "original-test-credential") == 0);
    request->result = 401;
    deliver(&fixture, request);
    mybot_device_lifecycle_request_start(&fixture.lifecycle);
    ticks(&fixture, 9);
    assert(fixture.queued_count == 0 && stored(&fixture, "tr_session"));
    assert(strcmp(fixture.lifecycle.error_code, "CLEANUP_AUTH_REJECTED") == 0);
    dispose(&fixture);
}
static void stale_binding_poll_ignored(void) {
    fixture_t fixture;
    bound(&fixture);
    fixture.lifecycle.runtime_tick_counter = fixture.lifecycle.runtime_poll_interval * 10 - 1;
    ticks(&fixture, 1);
    mybot_device_service_request_t *request = take(&fixture, MYBOT_DEVICE_SERVICE_DEVICE_POLL);
    assert(request);
    mybot_device_lifecycle_set_network_available(&fixture.lifecycle, false);
    mybot_device_lifecycle_set_network_available(&fixture.lifecycle, true);
    request->result = 401;
    deliver(&fixture, request);
    assert(fixture.lifecycle.device_token[0]);
    assert(!fixture.lifecycle.poll_pending);
    dispose(&fixture);
}
static void queued_start_cancelled_before_http(void) {
    fixture_t fixture;
    bound(&fixture);
    mybot_device_service_request_t *request = starting(&fixture);
    mybot_device_lifecycle_shutdown(&fixture.lifecycle);
    request->result = -1;
    snprintf(request->payload.start.response.error_code,
             sizeof(request->payload.start.response.error_code), "START_CANCELLED");
    /* HTTP worker proves it never dispatched, so there is no unknown cloud outcome. */
    deliver(&fixture, request);
    assert(!stored(&fixture, "tr_session") && fixture.starts == 0);
    mybot_device_lifecycle_finish_shutdown(&fixture.lifecycle);
    initialize(&fixture, true, SERVER, DEVICE);
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_RUNTIME);
    dispose(&fixture);
}
static void unsynchronized_start_allows_retry_after_sync(void) {
    fixture_t fixture;
    bound(&fixture);
    host_now = 0;
    mybot_device_service_request_t *request = starting(&fixture);
    request->result = -1;
    snprintf(request->payload.start.response.error_code,
             sizeof(request->payload.start.response.error_code), "TIME_NOT_SYNCED");
    /* Client preflight returned before HTTP: remove the pre-dispatch journal. */
    deliver(&fixture, request);
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_RUNTIME);
    assert(strcmp(fixture.lifecycle.error_code, "TIME_NOT_SYNCED") == 0);
    assert(!stored(&fixture, "tr_session") && !fixture.lifecycle.pending_unknown);
    assert(!fixture.lifecycle.cleanup_needed && fixture.starts == 0);
    assert(strcmp(fixture.lifecycle.device_token, "original-test-credential") == 0);
    host_now = 1800000000;
    request = starting(&fixture);
    success_response(request, 27);
    deliver(&fixture, request);
    assert(fixture.lifecycle.state == MYBOT_DEVICE_STATE_IN_CONVERSATION);
    assert(fixture.starts == 1 && !fixture.lifecycle.error_code[0]);
    dispose(&fixture);
}
static void shutdown_confirmed_stop_does_not_duplicate(void) {
    fixture_t fixture;
    active(&fixture);
    mybot_device_lifecycle_shutdown(&fixture.lifecycle);
    assert(fixture.stops == 1);
    mybot_device_service_request_t *request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request);
    deliver(&fixture, request);
    unsigned before = synchronous_stops;
    mybot_device_lifecycle_finish_shutdown(&fixture.lifecycle);
    assert(before == synchronous_stops && !stored(&fixture, "tr_session"));
    dispose(&fixture);
}
static void invalid_initial_configuration(void) {
    fixture_t fixture;
    memset(&fixture, 0, sizeof(fixture));
    fixture.store.ctx = &fixture;
    assert(mybot_device_lifecycle_init(&fixture.lifecycle, &fixture.store, SERVER, DEVICE, "fw",
                                       "hw", "en-US", "en-US", NULL) == -1);
    assert(mybot_device_lifecycle_init(&fixture.lifecycle, &fixture.store, SERVER, DEVICE, "fw",
                                       "hw", "", "zh-CN", NULL) == -1);
    assert(mybot_device_lifecycle_init(&fixture.lifecycle, &fixture.store, SERVER, DEVICE, "fw",
                                       "hw", "en US", "zh-CN", NULL) == -1);
    assert(live_allocations == 0);
}

static void face_mode_is_volatile_and_idle_only(void) {
    fixture_t fixture;
    bound(&fixture);
    assert(fixture.lifecycle.mode == MYBOT_TRANSLATION_LISTEN);
    slot_t saved[ARRAY_COUNT(fixture.slots)];
    memcpy(saved, fixture.slots, sizeof(saved));
    unsigned configurations = fixture.configurations;
    assert(mybot_device_lifecycle_set_mode(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN_SPEAK) ==
           0);
    assert(fixture.lifecycle.mode == MYBOT_TRANSLATION_LISTEN_SPEAK);
    assert(fixture.configurations == configurations + 1);
    assert(!memcmp(saved, fixture.slots, sizeof(saved))); /* Mode never writes flash. */
    assert(mybot_device_lifecycle_set_mode(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN_SPEAK) ==
           0);
    assert(fixture.configurations == configurations + 1);
    assert(mybot_device_lifecycle_set_mode(&fixture.lifecycle, (mybot_translation_mode_t)2) < 0);
    assert(mybot_device_lifecycle_set_mode(NULL, MYBOT_TRANSLATION_LISTEN) < 0);
    bool *blocked[] = {&fixture.lifecycle.cleanup_needed, &fixture.lifecycle.pending_unknown,
                       &fixture.lifecycle.local_media_active};
    for (size_t i = 0; i < ARRAY_COUNT(blocked); ++i) {
        *blocked[i] = true;
        assert(mybot_device_lifecycle_set_mode(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN) < 0);
        assert(mybot_device_lifecycle_set_languages(&fixture.lifecycle, "zh-CN", "en-US") < 0);
        *blocked[i] = false;
    }
    mybot_device_lifecycle_request_start(&fixture.lifecycle);
    /* A physical PLAY request closes settings before the next control tick. */
    assert(mybot_device_lifecycle_set_mode(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN) < 0);
    assert(mybot_device_lifecycle_set_languages(&fixture.lifecycle, "zh-CN", "en-US") < 0);
    ticks(&fixture, 1);
    mybot_device_service_request_t *request = take(&fixture, MYBOT_DEVICE_SERVICE_START);
    assert(request);
    assert(request->payload.start.mode == MYBOT_TRANSLATION_LISTEN_SPEAK);
    assert(mybot_device_lifecycle_set_mode(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN) < 0);
    assert(request->payload.start.mode == MYBOT_TRANSLATION_LISTEN_SPEAK);
    success_response(request, 40);
    assert(request->payload.start.response.route_count == 2);
    deliver(&fixture, request);
    assert(fixture.starts == 1 && fixture.lifecycle.local_media_active);
    assert(mybot_device_lifecycle_set_mode(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN) < 0);
    request = stopping(&fixture);
    assert(mybot_device_lifecycle_set_mode(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN) < 0);
    deliver(&fixture, request);
    assert(fixture.stops == 1 && !stored(&fixture, "tr_session"));
    assert(mybot_device_lifecycle_set_mode(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN) == 0);
    assert(mybot_device_lifecycle_set_mode(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN_SPEAK) ==
           0);
    initialize(&fixture, true, SERVER, DEVICE);
    assert(fixture.lifecycle.mode == MYBOT_TRANSLATION_LISTEN);
    dispose(&fixture);
}

static void face_partial_start_cleans_one_parent(void) {
    fixture_t fixture;
    bound(&fixture);
    assert(mybot_device_lifecycle_set_mode(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN_SPEAK) ==
           0);
    mybot_device_service_request_t *request = starting(&fixture);
    request->result = 502;
    request->payload.start.response.cleanup_required = true;
    snprintf(request->payload.start.response.conversation_id,
             sizeof(request->payload.start.response.conversation_id), "face-partial-parent");
    snprintf(request->payload.start.response.error_code,
             sizeof(request->payload.start.response.error_code), "CONVERSATION_START_FAILED");
    deliver(&fixture, request);
    assert(!fixture.starts && stored(&fixture, "tr_session"));
    assert(mybot_device_lifecycle_set_mode(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN) < 0);
    ticks(&fixture, 1);
    request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request && !strcmp(request->payload.stop.conversation_id, "face-partial-parent"));
    request->result = 503;
    deliver(&fixture, request);
    mybot_device_lifecycle_request_start(&fixture.lifecycle);
    assert(mybot_device_lifecycle_set_mode(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN) < 0);
    ticks(&fixture, 50);
    request = take(&fixture, MYBOT_DEVICE_SERVICE_STOP);
    assert(request && !strcmp(request->payload.stop.conversation_id, "face-partial-parent"));
    assert(!take(&fixture, MYBOT_DEVICE_SERVICE_START));
    deliver(&fixture, request);
    assert(!stored(&fixture, "tr_session") && !fixture.starts);
    assert(fixture.lifecycle.mode == MYBOT_TRANSLATION_LISTEN_SPEAK);
    dispose(&fixture);
}

static void usb_selection_is_atomic_and_idle_only(void) {
    fixture_t fixture;
    bound(&fixture);
    unsigned before = fixture.configurations;
    assert(mybot_device_lifecycle_set_selection(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN_SPEAK,
                                                MYBOT_AUDIO_SOURCE_USB, "zh-CN", "en-US") == 0);
    assert(fixture.lifecycle.audio_source == MYBOT_AUDIO_SOURCE_USB);
    assert(fixture.lifecycle.mode == MYBOT_TRANSLATION_LISTEN_SPEAK);
    assert(!strcmp(fixture.lifecycle.source_language, "zh-CN"));
    assert(!strcmp(fixture.lifecycle.target_language, "en-US"));
    assert(fixture.configurations == before + 1);
    assert(mybot_device_lifecycle_set_mode(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN) < 0);
    mybot_device_service_request_t *request = starting(&fixture);
    assert(request->payload.start.mode == MYBOT_TRANSLATION_LISTEN_SPEAK);
    assert(!strcmp(request->payload.start.source_language, "zh-CN"));
    assert(mybot_device_lifecycle_set_selection(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN_SPEAK,
                                                MYBOT_AUDIO_SOURCE_CODEC, "zh-CN", "en-US") < 0);
    success_response(request, 80);
    deliver(&fixture, request);
    assert(fixture.starts == 1);
    assert(mybot_device_lifecycle_set_languages(&fixture.lifecycle, "ja-JP", "ko-KR") < 0);
    request = stopping(&fixture);
    deliver(&fixture, request);
    strcpy(fixture.fail_set, "tr_languages");
    before = fixture.configurations;
    assert(mybot_device_lifecycle_set_selection(&fixture.lifecycle, MYBOT_TRANSLATION_LISTEN_SPEAK,
                                                MYBOT_AUDIO_SOURCE_CODEC, "ja-JP", "ko-KR") < 0);
    assert(fixture.lifecycle.mode == MYBOT_TRANSLATION_LISTEN_SPEAK);
    assert(fixture.lifecycle.audio_source == MYBOT_AUDIO_SOURCE_USB);
    assert(!strcmp(fixture.lifecycle.source_language, "zh-CN"));
    assert(fixture.configurations == before);
    dispose(&fixture);
}

int main(void) {
    usb_selection_is_atomic_and_idle_only();
    face_mode_is_volatile_and_idle_only();
    face_partial_start_cleans_one_parent();
    successful_stop_clears_cleanup_status();
    repeated_start_stop();
    cancelled_late_start();
    disconnected_late_start();
    unknown_start_persists();
    unknown_crash_before_response();
    restart_uses_original_identity();
    failed_stop_backoff();
    failed_start_with_cleanup();
    definite_rejection_allows_retry();
    storage_failure_before_start();
    storage_failure_after_start();
    storage_erase_failure_retains_id();
    rejected_submission();
    expiry_and_callback();
    shutdown_late_start();
    shutdown_active_offline();
    re_pair_preserves_pending();
    language_persistence();
    direct_language_selection();
    corrupt_and_unreadable_journal();
    legacy_journal_recovery();
    compact_journal_roundtrip();
    compact_journal_rejects_malformed();
    compact_journal_fits_existing_nvs();
    duplicate_and_stale_results();
    allocation_failure();
    binding_revocation_retains_cleanup_identity();
    stale_binding_poll_ignored();
    queued_start_cancelled_before_http();
    unsynchronized_start_allows_retry_after_sync();
    shutdown_confirmed_stop_does_not_duplicate();
    invalid_initial_configuration();
    puts("translation lifecycle: 36 fault/lifecycle scenarios passed (UBSan)");
    return 0;
}
