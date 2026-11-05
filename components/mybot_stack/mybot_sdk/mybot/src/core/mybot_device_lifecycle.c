/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_device_lifecycle.h"

#include <api/aosl_atomic.h>
#include <api/aosl_log.h>
#include <hal/aosl_hal_memory.h>
#include <mybot/mybot_ui.h>
#include <mybot/platform/mybot_kv_store.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#define DEVICE_AUTH_KEY "device_auth"
#define PENDING_SESSION_KEY "tr_session"
#define LANGUAGE_KEY "tr_languages"
#define RECORD_VERSION 1U
#define PENDING_RECORD_VERSION 2U
#define PENDING_UNKNOWN 1U
#define PENDING_KNOWN 2U
#define PAIR_RETRY_INITIAL_TICKS 30
#define PAIR_RETRY_MAX_TICKS 600
#define STOP_RETRY_INITIAL_TICKS 10
#define STOP_RETRY_MAX_TICKS 300

/* The original auth layout stays unchanged so an existing Global binding can be reused. */
typedef struct {
    uint32_t version;
    char server_base[MYBOT_DEVICE_CLIENT_MAX_URL];
    char device_id[MYBOT_DEVICE_CLIENT_MAX_ID];
    char device_token[MYBOT_DEVICE_CLIENT_MAX_TOKEN];
} device_auth_record_t;

typedef struct {
    uint32_t version;
    uint32_t phase;
    char server_base[MYBOT_DEVICE_CLIENT_MAX_URL];
    char device_id[MYBOT_DEVICE_CLIENT_MAX_ID];
    char credential[MYBOT_DEVICE_CLIENT_MAX_TOKEN];
    char conversation_id[MYBOT_DEVICE_CLIENT_MAX_ID];
    char reason[32];
    int64_t expires_at;
} pending_session_v1_record_t;

/* Version 2 stores only the five terminated strings that are actually used.
 * A replacement NVS blob must fit alongside the previous journal until commit. */
typedef struct {
    uint32_t version;
    uint32_t phase;
    int64_t expires_at;
} pending_session_header_t;

_Static_assert(sizeof(pending_session_header_t) == 16, "Pending journal header must be 16 bytes");
_Static_assert(sizeof(pending_session_v1_record_t) == 1456,
               "The version 1 journal must remain readable");

typedef struct {
    uint32_t version;
    char source[33];
    char target[33];
} language_record_t;

typedef enum {
    STOP_NONE,
    STOP_HANGUP,
    STOP_ERROR,
    STOP_TIMEOUT,
} stop_request_t;

static const char *const state_names[] = {
    "unprovisioned", "pairing",  "awaiting_claim",  "runtime",       "in_conversation",
    "starting",      "stopping", "cleanup_pending", "start_unknown", "error",
};

static mybot_device_state_t state(const mybot_device_lifecycle_t *lifecycle) {
    return (mybot_device_state_t)aosl_atomic_read(&lifecycle->state);
}

static void set_state(mybot_device_lifecycle_t *lifecycle, mybot_device_state_t next) {
    if (state(lifecycle) == next)
        return;
    aosl_atomic_set(&lifecycle->state, next);
    AOSL_LOG_NTC("%s", state_names[next]);
    if (lifecycle->cbs.on_state_changed)
        lifecycle->cbs.on_state_changed(next, lifecycle->cbs.user_data);
}

static bool string_valid(const char *text, size_t capacity, bool required) {
    return text && memchr(text, '\0', capacity) && (!required || text[0]);
}

static bool language_valid(const char *text) {
    if (!string_valid(text, 33, true))
        return false;
    for (size_t i = 0; text[i]; ++i) {
        char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-'))
            return false;
    }
    return true;
}

static bool network_current(const mybot_device_lifecycle_t *lifecycle, intptr_t generation) {
    return aosl_atomic_read(&lifecycle->network_available) &&
           aosl_atomic_read(&lifecycle->network_generation) == generation &&
           !aosl_atomic_read(&lifecycle->network_loss_pending);
}

static int next_delay(int previous, int initial, int maximum) {
    return previous == 0 ? initial : (previous < maximum / 2 ? previous * 2 : maximum);
}

static int poll_interval(int seconds) {
    return seconds < 3 ? 3 : (seconds > 60 ? 60 : seconds);
}

static void storage_failed(mybot_device_lifecycle_t *lifecycle) {
    lifecycle->storage_fault = true;
    snprintf(lifecycle->error_code, sizeof(lifecycle->error_code), "STORAGE_ERROR");
    lifecycle->conversation_requested = false;
    AOSL_LOG_ERR("translation persistence failed; new starts disabled until restart");
    set_state(lifecycle, MYBOT_DEVICE_STATE_ERROR);
}

static void publish_idle(mybot_device_lifecycle_t *lifecycle) {
    if (lifecycle->storage_fault)
        set_state(lifecycle, MYBOT_DEVICE_STATE_ERROR);
    else if (lifecycle->device_token[0])
        set_state(lifecycle, MYBOT_DEVICE_STATE_RUNTIME);
    else {
        set_state(lifecycle, MYBOT_DEVICE_STATE_UNPROVISIONED);
        lifecycle->pairing_requested = true;
    }
}

static bool cleanup_error(const mybot_device_lifecycle_t *lifecycle) {
    return strcmp(lifecycle->error_code, "START_UNKNOWN") == 0 ||
           strcmp(lifecycle->error_code, "CLEANUP_PENDING") == 0 ||
           strcmp(lifecycle->error_code, "CLEANUP_AUTH_REJECTED") == 0;
}

static void publish_cleanup(mybot_device_lifecycle_t *lifecycle) {
    if (!lifecycle->error_code[0] || strcmp(lifecycle->error_code, "START_UNKNOWN") == 0 ||
        strcmp(lifecycle->error_code, "CLEANUP_PENDING") == 0)
        snprintf(lifecycle->error_code, sizeof(lifecycle->error_code), "%s",
                 lifecycle->pending_unknown ? "START_UNKNOWN" : "CLEANUP_PENDING");
    if (lifecycle->storage_fault)
        set_state(lifecycle, MYBOT_DEVICE_STATE_ERROR);
    else if (lifecycle->start_pending)
        set_state(lifecycle, MYBOT_DEVICE_STATE_STOPPING);
    else if (lifecycle->conversation_id[0])
        set_state(lifecycle, MYBOT_DEVICE_STATE_CLEANUP_PENDING);
    else
        set_state(lifecycle, MYBOT_DEVICE_STATE_START_UNKNOWN);
}

static int save_auth(mybot_device_lifecycle_t *lifecycle) {
    device_auth_record_t *record = aosl_hal_malloc(sizeof(*record));
    if (!record)
        return -1;
    memset(record, 0, sizeof(*record));
    record->version = RECORD_VERSION;
    memcpy(record->server_base, lifecycle->server_base, sizeof(record->server_base));
    memcpy(record->device_id, lifecycle->device_id, sizeof(record->device_id));
    memcpy(record->device_token, lifecycle->device_token, sizeof(record->device_token));
    int result = mybot_kv_store_set(lifecycle->kv_store, DEVICE_AUTH_KEY, record, sizeof(*record));
    aosl_hal_free(record);
    return result;
}

/* 1: restored, 0: no credential for this endpoint, -1: storage failure. */
static int load_auth(mybot_device_lifecycle_t *lifecycle) {
    device_auth_record_t *record = aosl_hal_malloc(sizeof(*record));
    if (!record)
        return -1;
    size_t length = 0;
    memset(record, 0, sizeof(*record));
    int result =
        mybot_kv_store_get(lifecycle->kv_store, DEVICE_AUTH_KEY, record, sizeof(*record), &length);
    if (result == MYBOT_ERR_NOT_FOUND) {
        aosl_hal_free(record);
        return 0;
    }
    if (result < 0) {
        aosl_hal_free(record);
        return -1;
    }
    bool valid = length == sizeof(*record) && record->version == RECORD_VERSION &&
                 string_valid(record->server_base, sizeof(record->server_base), true) &&
                 string_valid(record->device_id, sizeof(record->device_id), true) &&
                 string_valid(record->device_token, sizeof(record->device_token), true) &&
                 strcmp(record->server_base, lifecycle->server_base) == 0 &&
                 strcmp(record->device_id, lifecycle->device_id) == 0;
    if (valid)
        memcpy(lifecycle->device_token, record->device_token, sizeof(lifecycle->device_token));
    else if (mybot_kv_store_erase(lifecycle->kv_store, DEVICE_AUTH_KEY) < 0)
        result = -1;
    aosl_hal_free(record);
    return result < 0 ? -1 : (valid ? 1 : 0);
}

static void clear_auth(mybot_device_lifecycle_t *lifecycle) {
    ++lifecycle->binding_generation;
    lifecycle->device_token[0] = '\0';
    if (mybot_kv_store_erase(lifecycle->kv_store, DEVICE_AUTH_KEY) < 0)
        storage_failed(lifecycle);
}

static int save_pending(mybot_device_lifecycle_t *lifecycle) {
    const char *fields[] = {lifecycle->pending_server_base, lifecycle->pending_device_id,
                            lifecycle->pending_credential, lifecycle->conversation_id,
                            lifecycle->stop_reason};
    const size_t capacities[] = {
        sizeof(lifecycle->pending_server_base), sizeof(lifecycle->pending_device_id),
        sizeof(lifecycle->pending_credential), sizeof(lifecycle->conversation_id),
        sizeof(lifecycle->stop_reason)};
    size_t lengths[5];
    size_t length = sizeof(pending_session_header_t);
    for (size_t i = 0; i < 5; ++i) {
        const char *end = memchr(fields[i], '\0', capacities[i]);
        if (!end || (end == fields[i] && (i != 3 || !lifecycle->pending_unknown))) {
            AOSL_LOG_ERR("event=translation_storage action=save key=%s field=%zu result=invalid",
                         PENDING_SESSION_KEY, i);
            return -1;
        }
        lengths[i] = (size_t)(end - fields[i]) + 1;
        length += lengths[i];
    }
    if (length > sizeof(pending_session_v1_record_t))
        return -1;
    unsigned char *record = aosl_hal_malloc(length);
    if (!record) {
        AOSL_LOG_ERR("event=translation_storage action=allocate key=%s bytes=%zu phase=%s "
                     "result=error",
                     PENDING_SESSION_KEY, length, lifecycle->pending_unknown ? "unknown" : "known");
        return -1;
    }
    const pending_session_header_t header = {
        .version = PENDING_RECORD_VERSION,
        .phase = lifecycle->pending_unknown ? PENDING_UNKNOWN : PENDING_KNOWN,
        .expires_at = lifecycle->expires_at,
    };
    memcpy(record, &header, sizeof(header));
    size_t offset = sizeof(header);
    for (size_t i = 0; i < 5; ++i) {
        memcpy(record + offset, fields[i], lengths[i]);
        offset += lengths[i];
    }
    int result = mybot_kv_store_set(lifecycle->kv_store, PENDING_SESSION_KEY, record, length);
    aosl_hal_free(record);
    if (result == 0)
        lifecycle->journal_present = true;
    else
        AOSL_LOG_ERR("event=translation_storage action=save key=%s bytes=%zu phase=%s result=%d",
                     PENDING_SESSION_KEY, length, lifecycle->pending_unknown ? "unknown" : "known",
                     result);
    return result;
}

/* Never discard a malformed/unreadable journal: it might own a live cloud session. */
static int load_pending(mybot_device_lifecycle_t *lifecycle) {
    pending_session_v1_record_t *record = aosl_hal_malloc(sizeof(*record));
    if (!record)
        return -1;
    size_t length = 0;
    int result = mybot_kv_store_get(lifecycle->kv_store, PENDING_SESSION_KEY, record,
                                    sizeof(*record), &length);
    if (result == MYBOT_ERR_NOT_FOUND) {
        aosl_hal_free(record);
        return 0;
    }
    if (result != 0 || length < sizeof(uint32_t) || length > sizeof(*record))
        goto invalid;

    const char *fields[5];
    char *outputs[] = {lifecycle->pending_server_base, lifecycle->pending_device_id,
                       lifecycle->pending_credential, lifecycle->conversation_id,
                       lifecycle->stop_reason};
    const size_t capacities[] = {
        sizeof(lifecycle->pending_server_base), sizeof(lifecycle->pending_device_id),
        sizeof(lifecycle->pending_credential), sizeof(lifecycle->conversation_id),
        sizeof(lifecycle->stop_reason)};
    uint32_t version;
    uint32_t phase;
    int64_t expires_at;
    memcpy(&version, record, sizeof(version));
    if (version == RECORD_VERSION && length == sizeof(*record)) {
        phase = record->phase;
        expires_at = record->expires_at;
        fields[0] = record->server_base;
        fields[1] = record->device_id;
        fields[2] = record->credential;
        fields[3] = record->conversation_id;
        fields[4] = record->reason;
    } else if (version == PENDING_RECORD_VERSION && length >= sizeof(pending_session_header_t)) {
        pending_session_header_t header;
        memcpy(&header, record, sizeof(header));
        phase = header.phase;
        expires_at = header.expires_at;
        const char *cursor = (const char *)record + sizeof(header);
        const char *limit = (const char *)record + length;
        for (size_t i = 0; i < 5; ++i) {
            size_t remaining = (size_t)(limit - cursor);
            size_t available = remaining < capacities[i] ? remaining : capacities[i];
            const char *end = memchr(cursor, '\0', available);
            if (!end)
                goto invalid;
            fields[i] = cursor;
            cursor = end + 1;
        }
        if (cursor != limit)
            goto invalid;
    } else {
        goto invalid;
    }
    if (phase != PENDING_UNKNOWN && phase != PENDING_KNOWN)
        goto invalid;
    for (size_t i = 0; i < 5; ++i)
        if (!string_valid(fields[i], capacities[i], i != 3 || phase == PENDING_KNOWN))
            goto invalid;
    /* Only publish the decoded identity after every field and the full length
     * have been checked. Keep the original credential even after a rebind. */
    for (size_t i = 0; i < 5; ++i) {
        memset(outputs[i], 0, capacities[i]);
        memcpy(outputs[i], fields[i], strlen(fields[i]) + 1);
    }
    lifecycle->expires_at = expires_at;
    lifecycle->pending_unknown = phase == PENDING_UNKNOWN;
    lifecycle->journal_present = true;
    lifecycle->cleanup_needed = true;
    aosl_hal_free(record);
    return 1;
invalid:
    aosl_hal_free(record);
    return -1;
}

static int clear_pending(mybot_device_lifecycle_t *lifecycle) {
    if (mybot_kv_store_erase(lifecycle->kv_store, PENDING_SESSION_KEY) < 0) {
        storage_failed(lifecycle);
        return -1;
    }
    lifecycle->conversation_id[0] = '\0';
    lifecycle->pending_server_base[0] = '\0';
    lifecycle->pending_device_id[0] = '\0';
    lifecycle->pending_credential[0] = '\0';
    lifecycle->expires_at = 0;
    lifecycle->journal_present = false;
    lifecycle->pending_unknown = false;
    lifecycle->cleanup_needed = false;
    lifecycle->start_cancelled = false;
    lifecycle->stop_retry_delay_ticks = 0;
    lifecycle->stop_retry_ticks_remaining = 0;
    aosl_atomic_set(&lifecycle->stop_request, STOP_NONE);
    /* Completion resolves cleanup status, but must retain a genuine start/storage failure. */
    if (cleanup_error(lifecycle))
        lifecycle->error_code[0] = '\0';
    return 0;
}

static int load_languages(mybot_device_lifecycle_t *lifecycle) {
    language_record_t record;
    memset(&record, 0, sizeof(record));
    size_t length = 0;
    int result =
        mybot_kv_store_get(lifecycle->kv_store, LANGUAGE_KEY, &record, sizeof(record), &length);
    if (result == MYBOT_ERR_NOT_FOUND)
        return 0;
    if (result != 0 || length != sizeof(record) || record.version != RECORD_VERSION ||
        !language_valid(record.source) || !language_valid(record.target) ||
        strcmp(record.source, record.target) == 0)
        return -1;
    memcpy(lifecycle->source_language, record.source, sizeof(record.source));
    memcpy(lifecycle->target_language, record.target, sizeof(record.target));
    return 0;
}

static void configuration_changed(mybot_device_lifecycle_t *lifecycle) {
    if (lifecycle->cbs.on_configuration_changed)
        lifecycle->cbs.on_configuration_changed(
            lifecycle->source_language, lifecycle->target_language, lifecycle->cbs.user_data);
}

static mybot_device_service_request_t *new_request(mybot_device_lifecycle_t *lifecycle,
                                                   mybot_device_service_request_type_t type) {
    mybot_device_service_request_t *request = aosl_hal_malloc(sizeof(*request));
    if (!request)
        return NULL;
    memset(request, 0, sizeof(*request));
    request->type = type;
    request->network_generation = aosl_atomic_read(&lifecycle->network_generation);
    request->binding_generation = lifecycle->binding_generation;
    request->request_generation = ++lifecycle->request_generation;
    memcpy(request->server_base, lifecycle->server_base, sizeof(request->server_base));
    memcpy(request->device_id, lifecycle->device_id, sizeof(request->device_id));
    memcpy(request->credential,
           type == MYBOT_DEVICE_SERVICE_PAIR_POLL ? lifecycle->pair_token : lifecycle->device_token,
           sizeof(request->credential));
    return request;
}

static bool submit(mybot_device_lifecycle_t *lifecycle, mybot_device_service_request_t *request) {
    if (!lifecycle->cbs.submit_service_request ||
        lifecycle->cbs.submit_service_request(request, lifecycle->cbs.user_data) < 0) {
        aosl_hal_free(request);
        return false;
    }
    return true;
}

static void schedule_stop_retry(mybot_device_lifecycle_t *lifecycle) {
    lifecycle->stop_retry_delay_ticks = next_delay(lifecycle->stop_retry_delay_ticks,
                                                   STOP_RETRY_INITIAL_TICKS, STOP_RETRY_MAX_TICKS);
    lifecycle->stop_retry_ticks_remaining = lifecycle->stop_retry_delay_ticks;
    publish_cleanup(lifecycle);
}

static void queue_stop(mybot_device_lifecycle_t *lifecycle) {
    if (!lifecycle->conversation_id[0] || lifecycle->stop_pending ||
        !aosl_atomic_read(&lifecycle->network_available))
        return;
    mybot_device_service_request_t *request = new_request(lifecycle, MYBOT_DEVICE_SERVICE_STOP);
    if (!request) {
        schedule_stop_retry(lifecycle);
        return;
    }
    memcpy(request->server_base, lifecycle->pending_server_base, sizeof(request->server_base));
    memcpy(request->device_id, lifecycle->pending_device_id, sizeof(request->device_id));
    memcpy(request->credential, lifecycle->pending_credential, sizeof(request->credential));
    memcpy(request->payload.stop.conversation_id, lifecycle->conversation_id,
           sizeof(request->payload.stop.conversation_id));
    memcpy(request->payload.stop.reason, lifecycle->stop_reason,
           sizeof(request->payload.stop.reason));
    lifecycle->stop_generation = request->request_generation;
    lifecycle->stop_pending = true;
    if (!lifecycle->storage_fault)
        set_state(lifecycle, MYBOT_DEVICE_STATE_STOPPING);
    if (!submit(lifecycle, request)) {
        lifecycle->stop_pending = false;
        schedule_stop_retry(lifecycle);
    }
}

static void begin_stop(mybot_device_lifecycle_t *lifecycle, const char *reason) {
    lifecycle->conversation_requested = false;
    if (lifecycle->start_pending)
        lifecycle->start_cancelled = true;
    if (lifecycle->journal_present || lifecycle->pending_unknown || lifecycle->conversation_id[0]) {
        lifecycle->cleanup_needed = true;
        snprintf(lifecycle->stop_reason, sizeof(lifecycle->stop_reason), "%s", reason);
        publish_cleanup(lifecycle);
    }
    bool was_active = lifecycle->local_media_active;
    lifecycle->local_media_active = false;
    aosl_atomic_set(&lifecycle->stop_request, STOP_NONE);
    if (was_active && lifecycle->cbs.on_conversation_stop)
        lifecycle->cbs.on_conversation_stop(lifecycle->cbs.user_data);
    if (lifecycle->conversation_id[0] && save_pending(lifecycle) < 0)
        storage_failed(lifecycle);
}

static void start_session(mybot_device_lifecycle_t *lifecycle) {
    lifecycle->error_code[0] = '\0';
    mybot_device_service_request_t *request = new_request(lifecycle, MYBOT_DEVICE_SERVICE_START);
    if (!request)
        return;
    request->payload.start.mode = lifecycle->mode;
    memcpy(request->payload.start.source_language, lifecycle->source_language,
           sizeof(request->payload.start.source_language));
    memcpy(request->payload.start.target_language, lifecycle->target_language,
           sizeof(request->payload.start.target_language));
    memcpy(lifecycle->pending_server_base, request->server_base,
           sizeof(lifecycle->pending_server_base));
    memcpy(lifecycle->pending_device_id, request->device_id, sizeof(lifecycle->pending_device_id));
    memcpy(lifecycle->pending_credential, request->credential,
           sizeof(lifecycle->pending_credential));
    snprintf(lifecycle->stop_reason, sizeof(lifecycle->stop_reason), "%s",
             MYBOT_CONVERSATION_STOP_REASON_DEVICE_HANGUP);
    lifecycle->pending_unknown = true;
    /* Persist BEFORE dispatch: a reset or lost HTTP response must not permit another start. */
    if (save_pending(lifecycle) < 0) {
        aosl_hal_free(request);
        storage_failed(lifecycle);
        return;
    }
    lifecycle->start_generation = request->request_generation;
    lifecycle->start_pending = true;
    lifecycle->start_cancelled = false;
    aosl_atomic_set(&lifecycle->stop_request, STOP_NONE);
    set_state(lifecycle, MYBOT_DEVICE_STATE_STARTING);
    if (!submit(lifecycle, request)) {
        lifecycle->start_pending = false;
        /* Queue rejection proves no HTTP start was executed. */
        if (clear_pending(lifecycle) == 0)
            publish_idle(lifecycle);
    }
}

static bool pending_identity_matches(const mybot_device_lifecycle_t *lifecycle,
                                     const mybot_device_service_request_t *request) {
    return strcmp(request->server_base, lifecycle->pending_server_base) == 0 &&
           strcmp(request->device_id, lifecycle->pending_device_id) == 0 &&
           strcmp(request->credential, lifecycle->pending_credential) == 0;
}

static void apply_start(mybot_device_lifecycle_t *lifecycle,
                        const mybot_device_service_request_t *request) {
    if (!lifecycle->start_pending || request->request_generation != lifecycle->start_generation ||
        !pending_identity_matches(lifecycle, request))
        return;
    lifecycle->start_pending = false;
    const mybot_device_conversation_t *response = &request->payload.start.response;
    if (string_valid(response->error_code, sizeof(response->error_code), true))
        memcpy(lifecycle->error_code, response->error_code, sizeof(lifecycle->error_code));
    bool known = string_valid(response->conversation_id, sizeof(response->conversation_id), true);
    AOSL_LOG_NTC("event=translation action=start phase=result service_result=%d error=%s "
                 "parent_known=%d cleanup_required=%d start_unknown=%d",
                 request->result, lifecycle->error_code[0] ? lifecycle->error_code : "none", known,
                 response->cleanup_required, response->start_unknown);
    if (known) {
        memcpy(lifecycle->conversation_id, response->conversation_id,
               sizeof(lifecycle->conversation_id));
        lifecycle->pending_unknown = false;
        lifecycle->expires_at = response->expires_at;
        /* Save even a cancelled, stale-network, malformed or rejected start's cleanup ID. */
        if (save_pending(lifecycle) < 0)
            storage_failed(lifecycle);
        bool usable = request->result == 0 && !response->cleanup_required &&
                      !response->start_unknown && !lifecycle->storage_fault &&
                      !lifecycle->start_cancelled && !lifecycle->pairing_requested &&
                      !aosl_atomic_read(&lifecycle->shutting_down) &&
                      network_current(lifecycle, request->network_generation) &&
                      lifecycle->binding_generation == request->binding_generation &&
                      lifecycle->expires_at > (int64_t)time(NULL);
        if (!usable) {
            AOSL_LOG_WRN("event=translation action=start result=cleanup cancelled=%d "
                         "network_current=%d binding_current=%d shutting_down=%d "
                         "storage_fault=%d expired=%d",
                         lifecycle->start_cancelled,
                         network_current(lifecycle, request->network_generation),
                         lifecycle->binding_generation == request->binding_generation,
                         (int)aosl_atomic_read(&lifecycle->shutting_down), lifecycle->storage_fault,
                         lifecycle->expires_at <= (int64_t)time(NULL));
            begin_stop(lifecycle, MYBOT_CONVERSATION_STOP_REASON_ERROR);
            return;
        }
        lifecycle->error_code[0] = '\0';
        lifecycle->local_media_active = true;
        lifecycle->cleanup_needed = false;
        set_state(lifecycle, MYBOT_DEVICE_STATE_IN_CONVERSATION);
        if (lifecycle->cbs.on_conversation_start)
            lifecycle->cbs.on_conversation_start(response, lifecycle->cbs.user_data);
        return;
    }
    if (response->start_unknown || response->cleanup_required || request->result == 0) {
        lifecycle->pending_unknown = true;
        lifecycle->cleanup_needed = true;
        publish_cleanup(lifecycle);
        AOSL_LOG_ERR("start outcome unknown; repeated start blocked pending server reconciliation");
        return;
    }
    /* A definite HTTP rejection without a parent ID did not start a session. */
    if (clear_pending(lifecycle) < 0)
        return;
    if (request->result == 401 ||
        (request->result == 409 && strcmp(response->error_code, "DEVICE_NOT_BOUND") == 0)) {
        clear_auth(lifecycle);
        lifecycle->pairing_requested = true;
    }
    publish_idle(lifecycle);
}

static void apply_stop(mybot_device_lifecycle_t *lifecycle,
                       const mybot_device_service_request_t *request) {
    if (!lifecycle->stop_pending || request->request_generation != lifecycle->stop_generation ||
        !pending_identity_matches(lifecycle, request) ||
        strcmp(request->payload.stop.conversation_id, lifecycle->conversation_id) != 0)
        return;
    lifecycle->stop_pending = false;
    /* The client returns zero ONLY after HTTP 200 and conversation.status == ended. */
    if (request->result == 0 && clear_pending(lifecycle) == 0) {
        AOSL_LOG_NTC("translation cloud cleanup confirmed");
        publish_idle(lifecycle);
    } else {
        /* Includes revoked credentials: preserve the original cleanup identity and parent ID. */
        AOSL_LOG_WRN(
            "event=translation action=stop result=retry service_result=%d storage_fault=%d",
            request->result, lifecycle->storage_fault);
        if (!lifecycle->storage_fault && (!lifecycle->error_code[0] || cleanup_error(lifecycle)))
            snprintf(lifecycle->error_code, sizeof(lifecycle->error_code), "%s",
                     request->result == 401 || request->result == 403 ? "CLEANUP_AUTH_REJECTED"
                                                                      : "CLEANUP_PENDING");
        schedule_stop_retry(lifecycle);
    }
}

static void create_pair_code(mybot_device_lifecycle_t *lifecycle) {
    mybot_device_pair_code_t response;
    memset(&response, 0, sizeof(response));
    intptr_t generation = aosl_atomic_read(&lifecycle->network_generation);
    int result = mybot_device_client_create_pair_code(lifecycle->server_base, lifecycle->device_id,
                                                      lifecycle->firmware_ver, lifecycle->hw_model,
                                                      &response);
    if (!network_current(lifecycle, generation) || result != 0) {
        lifecycle->pair_retry_delay_ticks = next_delay(
            lifecycle->pair_retry_delay_ticks, PAIR_RETRY_INITIAL_TICKS, PAIR_RETRY_MAX_TICKS);
        lifecycle->pair_retry_ticks_remaining = lifecycle->pair_retry_delay_ticks;
        set_state(lifecycle, MYBOT_DEVICE_STATE_UNPROVISIONED);
        return;
    }
    lifecycle->pair_retry_delay_ticks = 0;
    lifecycle->pair_retry_ticks_remaining = 0;
    ++lifecycle->binding_generation;
    memcpy(lifecycle->pair_token, response.pair_token, sizeof(lifecycle->pair_token));
    lifecycle->pair_poll_interval = poll_interval(response.poll_after_seconds);
    lifecycle->pair_tick_counter = 0;
    if (lifecycle->cbs.on_pair_code)
        lifecycle->cbs.on_pair_code(response.code, lifecycle->cbs.user_data);
    set_state(lifecycle, MYBOT_DEVICE_STATE_AWAITING_CLAIM);
}

static void poll_binding(mybot_device_lifecycle_t *lifecycle,
                         mybot_device_service_request_type_t type) {
    if (lifecycle->poll_pending)
        return;
    mybot_device_service_request_t *request = new_request(lifecycle, type);
    if (!request)
        return;
    lifecycle->poll_pending = true;
    lifecycle->poll_generation = request->request_generation;
    if (!submit(lifecycle, request))
        lifecycle->poll_pending = false;
}

static void apply_pair_binding(mybot_device_lifecycle_t *lifecycle,
                               const mybot_device_service_request_t *request) {
    if (request->result == 401) {
        lifecycle->pairing_requested = true;
        return;
    }
    if (request->result != 0)
        return;
    const mybot_device_binding_t *response = &request->payload.binding;
    if (strcmp(response->status, "pending") == 0) {
        lifecycle->pair_poll_interval = poll_interval(response->poll_after_seconds);
    } else if (strcmp(response->status, "bound") == 0) {
        if (response->device_token[0])
            memcpy(lifecycle->device_token, response->device_token,
                   sizeof(lifecycle->device_token));
        if (!lifecycle->device_token[0])
            return;
        if (save_auth(lifecycle) < 0) {
            storage_failed(lifecycle);
            return;
        }
        ++lifecycle->binding_generation;
        lifecycle->runtime_poll_interval = poll_interval(response->poll_after_seconds);
        lifecycle->runtime_tick_counter = 0;
        set_state(lifecycle, MYBOT_DEVICE_STATE_RUNTIME);
    } else if (strcmp(response->status, "expired") == 0 ||
               strcmp(response->status, "unbound") == 0 ||
               strcmp(response->status, "failed") == 0) {
        lifecycle->pairing_requested = true;
    }
}

static void apply_runtime_binding(mybot_device_lifecycle_t *lifecycle,
                                  const mybot_device_service_request_t *request) {
    const mybot_device_binding_t *response = &request->payload.binding;
    if (request->result == 401 ||
        (request->result == 0 && strcmp(response->status, "unbound") == 0)) {
        begin_stop(lifecycle, MYBOT_CONVERSATION_STOP_REASON_ERROR);
        clear_auth(lifecycle);
        lifecycle->pairing_requested = true;
        if (!lifecycle->journal_present)
            publish_idle(lifecycle);
    } else if (request->result == 0 && strcmp(response->status, "bound") == 0) {
        lifecycle->runtime_poll_interval = poll_interval(response->poll_after_seconds);
    }
}

int mybot_device_lifecycle_init(mybot_device_lifecycle_t *lifecycle, mybot_kv_store_t *kv_store,
                                const char *server_base, const char *device_id,
                                const char *firmware_ver, const char *hw_model,
                                const char *source_language, const char *target_language,
                                const mybot_device_lifecycle_callbacks_t *callbacks) {
    if (!lifecycle || !kv_store ||
        !string_valid(server_base, sizeof(lifecycle->server_base), true) ||
        !string_valid(device_id, sizeof(lifecycle->device_id), true) ||
        (firmware_ver && !string_valid(firmware_ver, sizeof(lifecycle->firmware_ver), false)) ||
        (hw_model && !string_valid(hw_model, sizeof(lifecycle->hw_model), false)) ||
        !language_valid(source_language) || !language_valid(target_language) ||
        strcmp(source_language, target_language) == 0)
        return -1;
    memset(lifecycle, 0, sizeof(*lifecycle));
    lifecycle->kv_store = kv_store;
    if (callbacks)
        lifecycle->cbs = *callbacks;
    snprintf(lifecycle->server_base, sizeof(lifecycle->server_base), "%s", server_base);
    snprintf(lifecycle->device_id, sizeof(lifecycle->device_id), "%s", device_id);
    snprintf(lifecycle->firmware_ver, sizeof(lifecycle->firmware_ver), "%s",
             firmware_ver ? firmware_ver : "");
    snprintf(lifecycle->hw_model, sizeof(lifecycle->hw_model), "%s", hw_model ? hw_model : "");
    snprintf(lifecycle->source_language, sizeof(lifecycle->source_language), "%s", source_language);
    snprintf(lifecycle->target_language, sizeof(lifecycle->target_language), "%s", target_language);
    aosl_atomic_set(&lifecycle->network_available, true);
    lifecycle->runtime_poll_interval = 30;
    int pending = load_pending(lifecycle);
    int auth = load_auth(lifecycle);
    int language_result = load_languages(lifecycle);
    configuration_changed(lifecycle);
    if (pending < 0 || auth < 0 || language_result < 0)
        storage_failed(lifecycle);
    else if (pending > 0)
        publish_cleanup(lifecycle);
    else
        publish_idle(lifecycle);
    return 0;
}

void mybot_device_lifecycle_service_result(mybot_device_lifecycle_t *lifecycle,
                                           const mybot_device_service_request_t *request) {
    if (!lifecycle || !request)
        return;
    /* These own cloud side effects: NEVER discard them just because network/binding changed. */
    if (request->type == MYBOT_DEVICE_SERVICE_START) {
        apply_start(lifecycle, request);
        return;
    }
    if (request->type == MYBOT_DEVICE_SERVICE_STOP) {
        apply_stop(lifecycle, request);
        return;
    }
    if (request->request_generation != lifecycle->poll_generation || !lifecycle->poll_pending)
        return;
    lifecycle->poll_pending = false;
    if (aosl_atomic_read(&lifecycle->shutting_down) ||
        !network_current(lifecycle, request->network_generation) ||
        lifecycle->binding_generation != request->binding_generation ||
        lifecycle->pairing_requested || lifecycle->storage_fault)
        return;
    if (request->type == MYBOT_DEVICE_SERVICE_PAIR_POLL &&
        state(lifecycle) == MYBOT_DEVICE_STATE_AWAITING_CLAIM)
        apply_pair_binding(lifecycle, request);
    else if (request->type == MYBOT_DEVICE_SERVICE_DEVICE_POLL &&
             (state(lifecycle) == MYBOT_DEVICE_STATE_RUNTIME || lifecycle->local_media_active))
        apply_runtime_binding(lifecycle, request);
}

void mybot_device_lifecycle_tick(mybot_device_lifecycle_t *lifecycle) {
    if (!lifecycle || aosl_atomic_read(&lifecycle->shutting_down))
        return;
    if (aosl_atomic_xchg(&lifecycle->network_loss_pending, false)) {
        lifecycle->conversation_requested = false;
        if (lifecycle->start_pending || lifecycle->local_media_active)
            begin_stop(lifecycle, MYBOT_CONVERSATION_STOP_REASON_ERROR);
    }
    if (lifecycle->pairing_requested && (lifecycle->start_pending || lifecycle->local_media_active))
        begin_stop(lifecycle, MYBOT_CONVERSATION_STOP_REASON_USER_REQUESTED);
    stop_request_t stop = (stop_request_t)aosl_atomic_xchg(&lifecycle->stop_request, STOP_NONE);
    if (stop != STOP_NONE) {
        const char *reason = stop == STOP_TIMEOUT  ? "timeout"
                             : stop == STOP_HANGUP ? MYBOT_CONVERSATION_STOP_REASON_DEVICE_HANGUP
                                                   : MYBOT_CONVERSATION_STOP_REASON_ERROR;
        begin_stop(lifecycle, reason);
    }
    if (lifecycle->local_media_active && lifecycle->expires_at > 0 &&
        (int64_t)time(NULL) >= lifecycle->expires_at)
        begin_stop(lifecycle, "timeout");

    if (lifecycle->cleanup_needed ||
        (lifecycle->journal_present && !lifecycle->local_media_active &&
         !lifecycle->start_pending)) {
        if (lifecycle->start_pending || lifecycle->pending_unknown ||
            !lifecycle->conversation_id[0])
            return;
        if (lifecycle->stop_retry_ticks_remaining > 0)
            --lifecycle->stop_retry_ticks_remaining;
        if (lifecycle->stop_retry_ticks_remaining == 0 &&
            aosl_atomic_read(&lifecycle->network_available))
            queue_stop(lifecycle);
        return;
    }
    if (lifecycle->storage_fault || lifecycle->start_pending ||
        !aosl_atomic_read(&lifecycle->network_available))
        return;
    if (lifecycle->pairing_requested) {
        lifecycle->conversation_requested = false;
        lifecycle->pairing_requested = false;
        clear_auth(lifecycle);
        if (!lifecycle->storage_fault) {
            set_state(lifecycle, MYBOT_DEVICE_STATE_PAIRING);
            create_pair_code(lifecycle);
        }
        return;
    }
    switch (state(lifecycle)) {
    case MYBOT_DEVICE_STATE_UNPROVISIONED:
        if (lifecycle->pair_retry_ticks_remaining > 0 &&
            --lifecycle->pair_retry_ticks_remaining == 0) {
            set_state(lifecycle, MYBOT_DEVICE_STATE_PAIRING);
            create_pair_code(lifecycle);
        }
        break;
    case MYBOT_DEVICE_STATE_AWAITING_CLAIM:
        if (++lifecycle->pair_tick_counter >= lifecycle->pair_poll_interval * 10) {
            lifecycle->pair_tick_counter = 0;
            poll_binding(lifecycle, MYBOT_DEVICE_SERVICE_PAIR_POLL);
        }
        break;
    case MYBOT_DEVICE_STATE_RUNTIME:
        if (lifecycle->conversation_requested) {
            lifecycle->conversation_requested = false;
            start_session(lifecycle);
            break;
        }
        /* fall through */
    case MYBOT_DEVICE_STATE_IN_CONVERSATION:
        if (++lifecycle->runtime_tick_counter >= lifecycle->runtime_poll_interval * 10) {
            lifecycle->runtime_tick_counter = 0;
            poll_binding(lifecycle, MYBOT_DEVICE_SERVICE_DEVICE_POLL);
        }
        break;
    default:
        break;
    }
}

void mybot_device_lifecycle_set_network_available(mybot_device_lifecycle_t *lifecycle,
                                                  bool available) {
    if (!lifecycle)
        return;
    bool was_available = aosl_atomic_xchg(&lifecycle->network_available, available) != 0;
    if (!available && was_available) {
        aosl_atomic_inc(&lifecycle->network_generation);
        aosl_atomic_set(&lifecycle->network_loss_pending, true);
    }
}

void mybot_device_lifecycle_shutdown(mybot_device_lifecycle_t *lifecycle) {
    if (!lifecycle)
        return;
    aosl_atomic_set(&lifecycle->shutting_down, true);
    lifecycle->conversation_requested = false;
    lifecycle->pairing_requested = false;
    if (lifecycle->start_pending || lifecycle->local_media_active || lifecycle->conversation_id[0])
        begin_stop(lifecycle, MYBOT_CONVERSATION_STOP_REASON_DEVICE_HANGUP);
    if (lifecycle->conversation_id[0])
        queue_stop(lifecycle);
}

void mybot_device_lifecycle_finish_shutdown(mybot_device_lifecycle_t *lifecycle) {
    if (!lifecycle)
        return;
    /* The caller joined HTTP first, so no request can still mutate/return a response. */
    lifecycle->start_pending = false;
    lifecycle->stop_pending = false;
    if (!lifecycle->conversation_id[0]) {
        if (lifecycle->pending_unknown)
            publish_cleanup(lifecycle);
        return;
    }
    if (!aosl_atomic_read(&lifecycle->network_available))
        return;
    int result = mybot_device_client_stop_conversation(
        lifecycle->pending_server_base, lifecycle->pending_device_id, lifecycle->pending_credential,
        lifecycle->conversation_id, lifecycle->stop_reason);
    if (result == 0 && clear_pending(lifecycle) == 0)
        publish_idle(lifecycle);
    else
        publish_cleanup(lifecycle);
}

void mybot_device_lifecycle_request_pair(mybot_device_lifecycle_t *lifecycle) {
    if (lifecycle && !aosl_atomic_read(&lifecycle->shutting_down))
        lifecycle->pairing_requested = true;
}

void mybot_device_lifecycle_request_start(mybot_device_lifecycle_t *lifecycle) {
    if (!lifecycle || aosl_atomic_read(&lifecycle->shutting_down) || lifecycle->storage_fault ||
        lifecycle->journal_present || lifecycle->pending_unknown || lifecycle->start_pending ||
        lifecycle->stop_pending || lifecycle->pairing_requested ||
        !aosl_atomic_read(&lifecycle->network_available) ||
        aosl_atomic_read(&lifecycle->network_loss_pending) ||
        state(lifecycle) != MYBOT_DEVICE_STATE_RUNTIME)
        return;
    lifecycle->conversation_requested = true;
}

void mybot_device_lifecycle_request_stop(mybot_device_lifecycle_t *lifecycle) {
    if (!lifecycle || aosl_atomic_read(&lifecycle->shutting_down))
        return;
    lifecycle->conversation_requested = false;
    if (lifecycle->start_pending)
        lifecycle->start_cancelled = true;
    if (lifecycle->start_pending || lifecycle->local_media_active)
        aosl_atomic_set(&lifecycle->stop_request, STOP_HANGUP);
}

void mybot_device_lifecycle_notify_conversation_ended(mybot_device_lifecycle_t *lifecycle) {
    if (lifecycle && !aosl_atomic_read(&lifecycle->shutting_down) &&
        state(lifecycle) == MYBOT_DEVICE_STATE_IN_CONVERSATION)
        aosl_atomic_set(&lifecycle->stop_request, STOP_ERROR);
}

void mybot_device_lifecycle_notify_credentials_expiring(mybot_device_lifecycle_t *lifecycle) {
    if (lifecycle && !aosl_atomic_read(&lifecycle->shutting_down) &&
        state(lifecycle) == MYBOT_DEVICE_STATE_IN_CONVERSATION)
        aosl_atomic_set(&lifecycle->stop_request, STOP_TIMEOUT);
}

int mybot_device_lifecycle_cycle_language(mybot_device_lifecycle_t *lifecycle, bool source) {
    if (!lifecycle || aosl_atomic_read(&lifecycle->shutting_down) || lifecycle->storage_fault ||
        lifecycle->journal_present || lifecycle->conversation_requested ||
        state(lifecycle) != MYBOT_DEVICE_STATE_RUNTIME)
        return -1;
    const char *current = source ? lifecycle->source_language : lifecycle->target_language;
    const char *other = source ? lifecycle->target_language : lifecycle->source_language;
    size_t count = 0;
    const mybot_ui_language_t *languages = mybot_ui_languages(&count);
    size_t selected = count - 1;
    for (size_t i = 0; i < count; ++i)
        if (strcmp(current, languages[i].code) == 0)
            selected = i;
    do {
        selected = (selected + 1) % count;
    } while (strcmp(other, languages[selected].code) == 0);
    return mybot_device_lifecycle_set_languages(
        lifecycle, source ? languages[selected].code : lifecycle->source_language,
        source ? lifecycle->target_language : languages[selected].code);
}

static bool ui_language_supported(const char *language) {
    if (!language_valid(language))
        return false;
    size_t count = 0;
    const mybot_ui_language_t *languages = mybot_ui_languages(&count);
    for (size_t i = 0; i < count; ++i)
        if (strcmp(language, languages[i].code) == 0)
            return true;
    return false;
}

int mybot_device_lifecycle_set_languages(mybot_device_lifecycle_t *lifecycle, const char *source,
                                         const char *target) {
    if (!lifecycle || aosl_atomic_read(&lifecycle->shutting_down) || lifecycle->storage_fault ||
        lifecycle->cleanup_needed || lifecycle->pending_unknown || lifecycle->conversation_id[0] ||
        lifecycle->local_media_active || lifecycle->journal_present ||
        lifecycle->conversation_requested || lifecycle->start_pending || lifecycle->stop_pending ||
        lifecycle->pairing_requested || state(lifecycle) != MYBOT_DEVICE_STATE_RUNTIME ||
        !ui_language_supported(source) || !ui_language_supported(target) ||
        strcmp(source, target) == 0)
        return -1;
    if (strcmp(source, lifecycle->source_language) == 0 &&
        strcmp(target, lifecycle->target_language) == 0)
        return 0;
    language_record_t record;
    memset(&record, 0, sizeof(record));
    record.version = RECORD_VERSION;
    snprintf(record.source, sizeof(record.source), "%s", source);
    snprintf(record.target, sizeof(record.target), "%s", target);
    if (mybot_kv_store_set(lifecycle->kv_store, LANGUAGE_KEY, &record, sizeof(record)) < 0) {
        storage_failed(lifecycle);
        return -1;
    }
    memcpy(lifecycle->source_language, record.source, sizeof(record.source));
    memcpy(lifecycle->target_language, record.target, sizeof(record.target));
    lifecycle->error_code[0] = '\0';
    configuration_changed(lifecycle);
    return 0;
}

int mybot_device_lifecycle_set_mode(mybot_device_lifecycle_t *lifecycle,
                                    mybot_translation_mode_t mode) {
    if (!lifecycle || aosl_atomic_read(&lifecycle->shutting_down) || lifecycle->storage_fault ||
        lifecycle->cleanup_needed || lifecycle->pending_unknown || lifecycle->conversation_id[0] ||
        lifecycle->local_media_active || lifecycle->journal_present ||
        lifecycle->conversation_requested || lifecycle->start_pending || lifecycle->stop_pending ||
        lifecycle->pairing_requested || state(lifecycle) != MYBOT_DEVICE_STATE_RUNTIME ||
        (mode != MYBOT_TRANSLATION_LISTEN && mode != MYBOT_TRANSLATION_LISTEN_SPEAK))
        return -1;
    if (lifecycle->mode == mode)
        return 0;
    lifecycle->mode = mode;
    lifecycle->error_code[0] = '\0';
    configuration_changed(lifecycle);
    return 0;
}
