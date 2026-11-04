/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_DEVICE_LIFECYCLE_H_
#define MYBOT_DEVICE_LIFECYCLE_H_

#include "mybot_device_client.h"
#include "mybot_device_state.h"
#include "mybot_kv_store_internal.h"

#include <api/aosl_atomic.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MYBOT_DEVICE_SERVICE_PAIR_POLL,
    MYBOT_DEVICE_SERVICE_DEVICE_POLL,
    MYBOT_DEVICE_SERVICE_START,
    MYBOT_DEVICE_SERVICE_STOP,
} mybot_device_service_request_type_t;

/** Heap-owned HTTP snapshot; only the control owner applies its result. */
typedef struct mybot_device_service_request {
    mybot_device_service_request_type_t type;
    intptr_t network_generation;
    uint32_t binding_generation;
    uint32_t request_generation;
    char server_base[MYBOT_DEVICE_CLIENT_MAX_URL];
    char device_id[MYBOT_DEVICE_CLIENT_MAX_ID];
    char credential[MYBOT_DEVICE_CLIENT_MAX_TOKEN];
    int result;
    union {
        mybot_device_binding_t binding;
        struct {
            mybot_translation_mode_t mode;
            char source_language[33];
            char target_language[33];
            mybot_device_conversation_t response;
        } start;
        struct {
            char conversation_id[MYBOT_DEVICE_CLIENT_MAX_ID];
            char reason[32];
        } stop;
    } payload;
    /** Shutdown owner drains this list after joining the HTTP worker. */
    struct mybot_device_service_request *retired_next;
} mybot_device_service_request_t;

typedef struct {
    void (*on_pair_code)(const char *code, void *user_data);
    /** Borrowed response remains valid only for the synchronous callback. */
    void (*on_conversation_start)(const mybot_device_conversation_t *params, void *user_data);
    /** Stop local media and leave RTC synchronously, before service cleanup. */
    void (*on_conversation_stop)(void *user_data);
    void (*on_state_changed)(mybot_device_state_t state, void *user_data);
    void (*on_configuration_changed)(const char *source_language, const char *target_language,
                                     void *user_data);
    /** Success transfers ownership; failure leaves it with the caller. */
    int (*submit_service_request)(mybot_device_service_request_t *request, void *user_data);
    void *user_data;
} mybot_device_lifecycle_callbacks_t;

/** State is owned by the control worker except the documented atomic mailboxes. */
typedef struct {
    char server_base[MYBOT_DEVICE_CLIENT_MAX_URL];
    char device_id[MYBOT_DEVICE_CLIENT_MAX_ID];
    char firmware_ver[64];
    char hw_model[64];
    char source_language[33];
    char target_language[33];
    /** Session selection is volatile; boot always starts with media/listen. */
    mybot_translation_mode_t mode;
    char error_code[64];
    mybot_device_lifecycle_callbacks_t cbs;
    mybot_kv_store_t *kv_store;
    aosl_atomic_t state;

    char pair_token[MYBOT_DEVICE_CLIENT_MAX_TOKEN];
    int pair_poll_interval;
    int pair_tick_counter;
    int pair_retry_delay_ticks;
    int pair_retry_ticks_remaining;
    char device_token[MYBOT_DEVICE_CLIENT_MAX_TOKEN];
    int runtime_poll_interval;
    int runtime_tick_counter;
    bool poll_pending;
    uint32_t poll_generation;
    uint32_t binding_generation;
    uint32_t request_generation;

    /* Cleanup always uses the identity which created the parent session. */
    char pending_server_base[MYBOT_DEVICE_CLIENT_MAX_URL];
    char pending_device_id[MYBOT_DEVICE_CLIENT_MAX_ID];
    char pending_credential[MYBOT_DEVICE_CLIENT_MAX_TOKEN];
    char conversation_id[MYBOT_DEVICE_CLIENT_MAX_ID];
    char stop_reason[32];
    int64_t expires_at;
    bool pending_unknown;
    bool journal_present;
    bool storage_fault;
    bool local_media_active;
    bool cleanup_needed;
    bool start_pending;
    bool start_cancelled;
    uint32_t start_generation;
    bool stop_pending;
    uint32_t stop_generation;
    int stop_retry_delay_ticks;
    int stop_retry_ticks_remaining;
    bool conversation_requested;
    aosl_atomic_t stop_request;
    bool pairing_requested;
    aosl_atomic_t shutting_down;
    aosl_atomic_t network_available;
    aosl_atomic_t network_loss_pending;
    aosl_atomic_t network_generation;
} mybot_device_lifecycle_t;

int mybot_device_lifecycle_init(mybot_device_lifecycle_t *lifecycle, mybot_kv_store_t *kv_store,
                                const char *server_base, const char *device_id,
                                const char *firmware_ver, const char *hw_model,
                                const char *source_language, const char *target_language,
                                const mybot_device_lifecycle_callbacks_t *cbs);
/** Serialized control-owner calls, every 100 ms. */
void mybot_device_lifecycle_tick(mybot_device_lifecycle_t *lifecycle);
/** START/STOP results must also be applied after cancellation, disconnect and shutdown. */
void mybot_device_lifecycle_service_result(mybot_device_lifecycle_t *lifecycle,
                                           const mybot_device_service_request_t *request);
void mybot_device_lifecycle_set_network_available(mybot_device_lifecycle_t *lifecycle,
                                                  bool available);
/** Close media/cancel start; retains pending cleanup and permits late HTTP results. */
void mybot_device_lifecycle_shutdown(mybot_device_lifecycle_t *lifecycle);
/** After joining the HTTP worker and applying retired results, before KV deinit.
 * Performs at most one synchronous stop; unfinished cleanup remains durable. */
void mybot_device_lifecycle_finish_shutdown(mybot_device_lifecycle_t *lifecycle);
void mybot_device_lifecycle_request_pair(mybot_device_lifecycle_t *lifecycle);
void mybot_device_lifecycle_request_start(mybot_device_lifecycle_t *lifecycle);
void mybot_device_lifecycle_request_stop(mybot_device_lifecycle_t *lifecycle);
void mybot_device_lifecycle_notify_conversation_ended(mybot_device_lifecycle_t *lifecycle);
/** RTC token-expiry notification ends the session; translation has no renewal API. */
void mybot_device_lifecycle_notify_credentials_expiring(mybot_device_lifecycle_t *lifecycle);
/** Cycle the local 15-language shortcut list while idle; durable before callback. */
int mybot_device_lifecycle_cycle_language(mybot_device_lifecycle_t *lifecycle, bool source);
/** Set both supported languages in one durable record while idle. */
int mybot_device_lifecycle_set_languages(mybot_device_lifecycle_t *lifecycle, const char *source,
                                         const char *target);
/** Select media or face-to-face only while idle; does not write KV. */
int mybot_device_lifecycle_set_mode(mybot_device_lifecycle_t *lifecycle,
                                    mybot_translation_mode_t mode);

#ifdef __cplusplus
}
#endif
#endif /* MYBOT_DEVICE_LIFECYCLE_H_ */
