/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_DEVICE_CLIENT_H_
#define MYBOT_DEVICE_CLIENT_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <mybot/mybot_translation.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------
 * Device service HTTP client
 * ---------------------------------------------------------- */

#define MYBOT_DEVICE_CLIENT_MAX_URL 640
/* Keep this capacity unchanged: it is part of the persisted device-auth layout. */
#define MYBOT_DEVICE_CLIENT_MAX_TOKEN 512
/* Agora RTC tokens may contain 512 bytes, plus the terminating NUL. */
#define MYBOT_DEVICE_CLIENT_MAX_RTC_TOKEN (MYBOT_DEVICE_CLIENT_MAX_TOKEN + 1)
#define MYBOT_DEVICE_CLIENT_MAX_ID 128

#define MYBOT_CONVERSATION_STOP_REASON_USER_REQUESTED "user_requested"
#define MYBOT_CONVERSATION_STOP_REASON_DEVICE_HANGUP "device_hangup"
#define MYBOT_CONVERSATION_STOP_REASON_ERROR "error"
#define MYBOT_CONVERSATION_STOP_REASON_TIMEOUT "timeout"

/** Pair-code response. */
typedef struct {
    char code[16]; /* 6-digit pair code */
    char pair_token[MYBOT_DEVICE_CLIENT_MAX_TOKEN];
    int poll_after_seconds; /* recommended poll interval */
} mybot_device_pair_code_t;

/** Device-binding response. */
typedef struct {
    char status[16]; /* pending | bound | unbound | expired | failed */
    char device_token[MYBOT_DEVICE_CLIENT_MAX_TOKEN];
    char agent_id[MYBOT_DEVICE_CLIENT_MAX_ID];
    char agent_name[128];
    int poll_after_seconds;
} mybot_device_binding_t;

/** One validated direction; each route owns its RTC identity and credentials. */
typedef struct {
    mybot_translation_direction_t direction;
    char rtc_app_id[64];
    char rtc_channel[128];
    char rtc_uid[64];       /* string UID assigned by server */
    char rtc_agent_uid[64]; /* string RTM peer UID assigned by server */
    char rtc_token[MYBOT_DEVICE_CLIENT_MAX_RTC_TOKEN];
    char audio_publisher_uid[64];
    char source_language[33];
    char target_language[33];
    bool tts_enabled;
    int64_t expires_at;
} mybot_device_translation_route_t;

/** Conversation-start response. Routes are normalized: listen first, then speak. */
typedef struct {
    char conversation_id[MYBOT_DEVICE_CLIENT_MAX_ID];
    mybot_translation_mode_t mode;
    unsigned route_count;
    mybot_device_translation_route_t routes[2];
    int64_t expires_at;    /* UTC epoch seconds */
    bool cleanup_required; /* Keep conversation_id until stop confirms ended. */
    bool start_unknown;    /* The server may have created a session without returning its ID. */
    char error_code[64];
} mybot_device_conversation_t;

/* ----------------------------------------------------------
 * Client calls - return 0 on success, a positive HTTP status code for a
 * non-2xx response, or -1 for transport/parsing/local failures.
 * ---------------------------------------------------------- */

/** Request a pairing code for the device. */
int mybot_device_client_create_pair_code(const char *base_url, const char *device_id,
                                         const char *firmware_ver, const char *hw_model,
                                         mybot_device_pair_code_t *resp);

/** Query the device binding status. */
int mybot_device_client_get_binding_status(const char *base_url, const char *device_id,
                                           const char *auth_header, mybot_device_binding_t *resp);

/** Start one translation parent. LISTEN uses input/output languages; LISTEN_SPEAK
 *  uses local/remote languages. Both directions are returned in normalized order.
 *  Inspect cleanup_required/start_unknown even on failure before attempting another start. */
int mybot_device_client_start_conversation(const char *base_url, const char *device_id,
                                           const char *device_token, mybot_translation_mode_t mode,
                                           const char *source_language, const char *target_language,
                                           mybot_device_conversation_t *resp);

/** Stop a conversation. */
int mybot_device_client_stop_conversation(const char *base_url, const char *device_id,
                                          const char *device_token, const char *conversation_id,
                                          const char *reason);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_DEVICE_CLIENT_H_ */
