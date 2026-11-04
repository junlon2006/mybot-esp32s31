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
    bool reject_submit;
    bool callback_ends_session;
    unsigned configurations;
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
int mybot_kv_store_set(mybot_kv_store_t *store, const char *key, const void *value, size_t length) {
    fixture_t *fixture = store->ctx;
    if (strcmp(fixture->fail_set, key) == 0)
        return -1;
    slot_t *entry = slot(fixture, key, true);
    assert(entry && length <= sizeof(entry->data));
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
    if (fixture->configurations > 0)
        assert(stored(fixture, "tr_languages"));
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
    snprintf(response->rtc_app_id, sizeof(response->rtc_app_id), "test-app");
    snprintf(response->rtc_channel, sizeof(response->rtc_channel), "test-channel");
    snprintf(response->rtc_uid, sizeof(response->rtc_uid), "TEST-LOCAL-IDENTITY");
    snprintf(response->rtc_agent_uid, sizeof(response->rtc_agent_uid), "TEST-AGENT-IDENTITY");
    snprintf(response->audio_publisher_uid, sizeof(response->audio_publisher_uid),
             "TEST-AGENT-IDENTITY");
    snprintf(response->source_language, sizeof(response->source_language), "%s",
             request->payload.start.source_language);
    snprintf(response->target_language, sizeof(response->target_language), "%s",
             request->payload.start.target_language);
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

int main(void) {
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
    duplicate_and_stale_results();
    allocation_failure();
    binding_revocation_retains_cleanup_identity();
    stale_binding_poll_ignored();
    queued_start_cancelled_before_http();
    unsynchronized_start_allows_retry_after_sync();
    shutdown_confirmed_stop_does_not_duplicate();
    invalid_initial_configuration();
    puts("translation lifecycle: 29 fault/lifecycle scenarios passed (UBSan)");
    return 0;
}
