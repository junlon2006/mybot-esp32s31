/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_H_
#define MYBOT_H_

#include <stdint.h>
#include <stdbool.h>
#include <mybot/mybot_errors.h>
#include <mybot/mybot_export.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MYBOT_TRANSLATION_LANGUAGE_CAPACITY 33

/**
 * @brief SDK configuration supplied by the host application.
 *
 * All string fields must be NUL-terminated within their fixed-size buffers;
 * mybot_start() validates this and rejects the configuration otherwise.
 * Empty (zero-length) strings are allowed only where noted.
 */
typedef struct {
    /** Device-service base URL. Must be a non-empty "https://" URL (or
     *  "http://" in an explicitly insecure development build). */
    char server_base[128];
    /** Stable device identifier supplied by the product. Must be non-empty.
     *  This product derives "ESP32-" plus 24 uppercase hexadecimal characters
     *  from the MAC HMAC-SHA256 digest in main/app_main.c. */
    char device_id[64];
    /** Firmware version string reported to the device service. Optional;
     *  may be empty. */
    char firmware_ver[32];
    /** Hardware model string reported to the device service. Optional;
     *  may be empty. */
    char hw_model[32];
    /** Initial input and translated-output language codes. Persisted device
     *  selections take precedence. Both must be non-empty, distinct identifiers
     *  containing only ASCII letters, digits and '-'. Supported languages depend
     *  on the service; the UI list is exposed by mybot_ui_languages(). */
    char source_language[MYBOT_TRANSLATION_LANGUAGE_CAPACITY];
    char target_language[MYBOT_TRANSLATION_LANGUAGE_CAPACITY];
} mybot_config_t;

/**
 * Application-level lifecycle state, returned by mybot_get_state().
 *
 * Describes the startup / runtime state of the whole SDK application instance,
 * including network readiness, service bring-up, device pairing,
 * conversation activity, connectivity, and shutdown.
 */
typedef enum {
    /** Not started, or the application control runtime has stopped. This does
     *  not prove every resource was reclaimed: failed teardown may retain
     *  resources. Also reported before mybot_start(). */
    MYBOT_STATE_STOPPED = 0,
    /** Waiting for the platform's initial MYBOT_WIFI_EVENT_STA_CONNECTED.
     *  Provisioning is owned by the host application. */
    MYBOT_STATE_WIFI_PROVISIONING,
    /** Wi-Fi is connected and the remaining startup services (KV storage,
     *  keys, audio capture/playback and the device-service state machine) are
     *  being initialized asynchronously. RTC is initialized on demand when a
     *  conversation starts. A failure here transitions to MYBOT_STATE_FAILED. */
    MYBOT_STATE_STARTING_SERVICES,
    /** Startup services are up and the device is bound with no pending cloud
     *  session cleanup. Start still checks local media/RTC readiness; the LCD
     *  error_code may describe a previous failure. */
    MYBOT_STATE_READY,
    /** Runtime Wi-Fi link was lost or failed;
     *  device-service traffic is paused and any active RTC conversation is
     *  ended locally. Returns to the corresponding online state on reconnect. */
    MYBOT_STATE_WIFI_DISCONNECTED,
    /** Unrecoverable failure during initial network readiness or service bring-up.
     *  The application should report the error and call mybot_stop(). */
    MYBOT_STATE_FAILED,
    /** Application shutdown is in progress. Failed worker joins retain
     *  resources for a later mybot_stop() attempt. */
    MYBOT_STATE_STOPPING,
    /** The device service has accepted a translation session. Includes local
     *  RTC setup and the active session; local audio readiness is reported
     *  separately by the UI snapshot. Teardown has its own states below. */
    MYBOT_STATE_IN_CONVERSATION,
    /** The device service is unprovisioned, requesting a pairing code, or
     *  waiting for the device to be claimed. Conversation start is not
     *  available until the device reaches MYBOT_STATE_READY. */
    MYBOT_STATE_PAIRING,
    /** A durable start record exists and the cloud start result is pending. */
    MYBOT_STATE_TRANSLATION_STARTING,
    /** A stop request for a known cloud parent session is pending. */
    MYBOT_STATE_TRANSLATION_STOPPING,
    /** A known cloud parent still needs confirmed cleanup; new starts are
     *  blocked and stop is retried with the original identity. */
    MYBOT_STATE_CLEANUP_PENDING,
    /** Cloud start may have succeeded but no cleanup parent ID is known.
     *  New starts remain blocked across reboot pending server reconciliation. */
    MYBOT_STATE_START_UNKNOWN,
    /** Device lifecycle failure, such as a storage fault. Inspect the UI
     *  error_code; a state value alone does not establish retry safety. */
    MYBOT_STATE_TRANSLATION_ERROR,
} mybot_state_t;

/**
 * @brief Initialize and start the application.
 *
 * Initializes the registered connectivity monitor without waiting for network
 * readiness or service startup. Platform init() runs before this call returns.
 * The control worker initializes the remaining services (KV storage, keys,
 * audio and the device-service state machine) after MYBOT_WIFI_EVENT_STA_CONNECTED.
 * RTC is initialized per translation session and finalized at session end.
 *
 * Preconditions:
 * - The host owns network provisioning and Wi-Fi credentials. Complete provisioning and
 *   establish usable connectivity before starting MyBot; the monitor must still report the
 *   initial connected event. Stop MyBot before entering provisioning again.
 * - Register one complete mybot_platform_descriptor_t first. Wi-Fi, KV, key,
 *   audio capture/playback and HTTPS are required by this S31 product.
 * - Plain "http://" is rejected unless MYBOT_ALLOW_INSECURE_HTTP=ON is set for
 *   development builds. This product supports only v2vt/listen and has no video.
 *
 * The configuration is validated (non-NULL cfg, NUL-terminated strings,
 * non-empty server_base / device_id, supported URL scheme). Calling
 * mybot_start() while the application is already active returns -1.
 *
 * @param cfg application configuration; must stay valid only for the duration
 *            of the call (it is copied).
 * @return 0 after the connectivity monitor starts, -1 on validation, admission
 *         or synchronous startup failure. Later service bring-up may still fail.
 *         A failed startup attempts cleanup; failed joins may retain resources.
 *
 * @note Call mybot_stop() after a failed start and before exiting. A new start
 *       is rejected while an application AOSL reference is still retained.
 *       Terminal RTSA cleanup failure blocks new translation starts until reboot.
 * @note mybot_start() acquires one application reference to the process-wide
 *       AOSL runtime. The matching reference is released by mybot_stop();
 *       applications that use AOSL directly must maintain their own ctor/dtor
 *       pair for the duration of that use.
 * @note Call from the main application thread, not from a platform event
 *       callback.
 * @note Calls to mybot_start() and mybot_stop() are serialized internally. A
 *       concurrent mybot_stop() waits for startup to finish; a concurrent
 *       mybot_start() observes the completed active state and returns -1.
 */
MYBOT_API int mybot_start(const mybot_config_t *cfg);

/**
 * @brief Check whether the application instance is still running.
 *
 * @return true from a successful mybot_start() until the application stops or
 *         an internal exit event clears the running flag; false otherwise (also when
 *         not started, after a failed start, or after stopping).
 *
 * @note Thread-safe (atomic read). The main loop should poll this and call
 *       mybot_stop() once it returns false.
 * @note Failure to queue a required control event also clears the running flag.
 *       Call mybot_stop() even if mybot_get_state() still reports an active state.
 */
MYBOT_API bool mybot_is_running(void);

/**
 * @brief Return the current application lifecycle state.
 *
 * @return One of the mybot_state_t values, including translation start, active,
 *         stop, cleanup, unknown-outcome and error states. Runtime Wi-Fi loss
 *         takes precedence over the device state until connectivity returns.
 *         Inspect the LCD snapshot for local audio readiness and error details.
 *
 * @note Thread-safe (atomic read).
 * @see mybot_state_t
 */
MYBOT_API mybot_state_t mybot_get_state(void);

/**
 * @brief Stop the application and release resources whose workers have exited.
 *
 * Signals shutdown, waits for worker exits and releases resources whose shutdown
 * succeeds. Product-owned network connections and Wi-Fi credentials remain outside
 * SDK ownership. A failed worker join retains its resources and application AOSL
 * reference for a later call to retry. Failed driver teardown remains driver-owned.
 *
 * Safe to call when not running, after a failed mybot_start(), or repeatedly.
 * Return does not guarantee complete reclamation or that a new start will succeed.
 * Cloud cleanup records survive shutdown until the service confirms the session
 * ended. Terminal vendor RTC finalization failure is not retried; reboot is required
 * before starting another translation.
 *
 * RTC shutdown is attempted before releasing the application AOSL reference.
 *
 * @warning Waits for worker shutdown, so it must be called from the
 *          main application thread — never from inside a platform event
 *          callback, an SDK worker thread, or a signal handler.
 * @note Calls concurrent with mybot_start() are serialized and wait for the
 *       startup attempt to finish before teardown begins.
 */
MYBOT_API void mybot_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_H_ */
