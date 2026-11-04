/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_AGORA_RTC_H_
#define MYBOT_AGORA_RTC_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <mybot/mybot_build_config.h>
#include "mybot_device_client.h"
#include <mybot/platform/mybot_video.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MYBOT_RTC_STATE_IDLE = 0,
    MYBOT_RTC_STATE_INITIALIZED,
    MYBOT_RTC_STATE_CONNECTING,
    MYBOT_RTC_STATE_CONNECTED,
    MYBOT_RTC_STATE_RECONNECTING,
    MYBOT_RTC_STATE_DISCONNECTED,
    MYBOT_RTC_STATE_ERROR,
} mybot_rtc_state_t;

/** RTM lifecycle events forwarded from the Agora SDK. */
typedef enum {
    MYBOT_RTM_EVENT_LOGIN = 0,
    MYBOT_RTM_EVENT_KICKOFF = 1,
    MYBOT_RTM_EVENT_EXIT = 2,
} mybot_rtm_event_type_t;

/** Maximum byte capacity of an RTM account including its terminating NUL. */
#define MYBOT_RTM_UID_MAX_LEN 64

/* rtc_mpq owns all mutable state. Vendor payloads are copied before dispatch;
 * application callbacks must not re-enter this API. */
typedef struct {
    void (*on_remote_audio)(mybot_translation_direction_t direction, uint32_t uid, const void *data,
                            size_t len, void *user_data);
    void (*on_token_will_expire)(void *user_data);
    void (*on_state_changed)(mybot_rtc_state_t state, void *user_data);
    void (*on_rtm_event)(const char *rtm_uid, mybot_rtm_event_type_t event_type, int error_code,
                         void *user_data);
    void (*on_rtm_data)(const char *rtm_uid, const void *data, size_t len, const char *custom_type,
                        void *user_data);
    void (*on_rtm_subscribe_result)(const char *channel, int error_code, void *user_data);
    void (*on_rtm_subscribe_data)(const char *channel, const char *rtm_uid, const void *data,
                                  size_t len, const char *custom_type, void *user_data);
#if MYBOT_ENABLE_VIDEO
    void (*on_video_key_frame_requested)(void *user_data);
    void (*on_video_target_bitrate_changed)(uint32_t target_bps, void *user_data);
#endif
    void *user_data;
} mybot_agora_rtc_callbacks_t;

/** Initialize a fresh RTSA service and forwarding queue for one conversation.
 * The previous conversation must have completed mybot_agora_rtc_fini(). */
int mybot_agora_rtc_init(const char *app_id, const mybot_agora_rtc_callbacks_t *callbacks);

/**
 * Validate an RTM account according to the Agora RTSA contract.
 *
 * The account must be non-empty, shorter than MYBOT_RTM_UID_MAX_LEN bytes, and
 * contain only ASCII letters, digits, space, or the punctuation characters
 * accepted by agora_rtm_login(). The account is normally the server-issued
 * rtc.uid/local_uid from the conversation response; do not derive it from the
 * device id unless the service explicitly assigns that value.
 */
bool mybot_agora_rtc_rtm_uid_is_valid(const char *rtm_uid);

/** Join one listen route or listen+speak routes on the same App ID/channel.
 * The listen route supplies the one RTM login; subscribe to the shared channel
 * once before joining each RTC identity. CONNECTED requires every RTC join and
 * every enabled TTS publisher subscription. Readiness/reconnect is bounded. */
int mybot_agora_rtc_join(const mybot_device_translation_route_t *routes, size_t route_count);

/** End the conversation, finalize RTSA and join the forwarding queue.
 * Cancel media sends before calling. A successful global RTSA fini supersedes
 * intermediate leave/destroy errors. Queue teardown can be retried; a failed
 * vendor global fini is terminal until device restart and is never repeated.
 * Returns zero only when both vendor and forwarding resources were released. */
int mybot_agora_rtc_fini(void);

/** Send one PCM payload on the specified route when all routes are ready. */
int mybot_agora_rtc_send_audio(mybot_translation_direction_t direction, const void *data,
                               size_t len);

#if MYBOT_ENABLE_VIDEO
/** Send one platform-encoded video frame on the active connection. */
int mybot_agora_rtc_send_video(const mybot_video_frame_t *frame);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_AGORA_RTC_H_ */
