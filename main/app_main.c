/* SPDX-License-Identifier: Apache-2.0 */
#include <mybot/mybot.h>
#include <mybot/mybot_version.h>
#include <agora_rtc_api.h>

#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "storage_init.h"
#include "psa/crypto.h"
#include "mybot_platform/board.h"
#include "mybot_platform/factory_reset.h"
#include "mybot_platform/network.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if CONFIG_MYBOT_DEBUG_RESOURCE_MONITOR
#include "resource_monitor.h"
#endif

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define TAG "mybot_bootstrap"
#define CONTROL_EVENT_WAIT_MS 100
#define STOP_RETRY_ATTEMPTS 3
#define STOP_RETRY_DELAY_MS 20
#define DEVICE_ID_DIGEST_BYTES 12

static const char k_device_id_hmac_key[] = "mybot-esp32-device-id-v1";

static int build_device_id(char *device_id, size_t capacity, const uint8_t mac[6]) {
    uint8_t digest[32];
    char digest_hex[DEVICE_ID_DIGEST_BYTES * 2 + 1];
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id = 0;
    size_t digest_length = 0;
    /* The digest keeps the ID stable without placing the raw MAC in requests or logs.
     * The compiled key is an identifier-obfuscation key, not an authentication secret. */
    if (!device_id || capacity == 0 || !mac) {
        return -1;
    }

    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_status_t status = psa_import_key(&attributes, (const uint8_t *)k_device_id_hmac_key,
                                         sizeof(k_device_id_hmac_key) - 1, &key_id);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) {
        return -1;
    }

    status = psa_mac_compute(key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256), mac, 6, digest, sizeof(digest),
                             &digest_length);
    psa_destroy_key(key_id);
    if (status != PSA_SUCCESS || digest_length != sizeof(digest)) {
        return -1;
    }

    for (size_t i = 0; i < DEVICE_ID_DIGEST_BYTES; ++i) {
        snprintf(&digest_hex[i * 2], 3, "%02X", digest[i]);
    }
    digest_hex[sizeof(digest_hex) - 1] = '\0';
    int written = snprintf(device_id, capacity, "ESP32-%s", digest_hex);
    return written > 0 && (size_t)written < capacity ? 0 : -1;
}

static int stop_mybot_bounded(void) {
    for (unsigned attempt = 0; attempt < STOP_RETRY_ATTEMPTS; ++attempt) {
        if (mybot_stop() == 0)
            return 0;
        ESP_LOGE(TAG, "event=stop_mybot attempt=%u result=retained", attempt + 1);
        if (attempt + 1 < STOP_RETRY_ATTEMPTS)
            vTaskDelay(pdMS_TO_TICKS(STOP_RETRY_DELAY_MS));
    }
    return -1;
}

void app_main(void) {
    const esp_app_desc_t *app_description = esp_app_get_description();
    ESP_LOGI(TAG, "mybot %s, Agora RTSA %s, ESP-IDF %s, build %s %s", mybot_version_string(),
             agora_rtc_get_version(), esp_get_idf_version(), app_description->date,
             app_description->time);

    if (mybot_factory_reset_handle_pending()) {
        return;
    }

    if (mybot_storage_init() < 0) {
        return;
    }
    if (mybot_network_bootstrap() < 0) {
        return;
    }

    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "PSRAM is required but was not initialized");
        return;
    }
#if CONFIG_MYBOT_DEBUG_RESOURCE_MONITOR
    (void)mybot_resource_monitor_start();
#endif
    if (mybot_board_register() < 0) {
        return;
    }

    const mybot_board_t *board = mybot_board_get();

    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        ESP_LOGE(TAG, "failed to read device MAC");
        return;
    }

    mybot_config_t config = {0};
    if (build_device_id(config.device_id, sizeof(config.device_id), mac) < 0) {
        ESP_LOGE(TAG, "failed to derive device ID from device MAC");
        return;
    }
    snprintf(config.server_base, sizeof(config.server_base), "%s", CONFIG_MYBOT_SERVER_BASE);
    snprintf(config.firmware_ver, sizeof(config.firmware_ver), "%s", app_description->version);
    snprintf(config.hw_model, sizeof(config.hw_model), "%s", board->hw_model);
    snprintf(config.source_language, sizeof(config.source_language), "%s",
             CONFIG_MYBOT_SOURCE_LANGUAGE);
    snprintf(config.target_language, sizeof(config.target_language), "%s",
             CONFIG_MYBOT_TARGET_LANGUAGE);

    (void)mybot_time_sync_start();
    for (;;) {
        int64_t network_started_us = esp_timer_get_time();
        ESP_LOGI(TAG, "event=control state=waiting_network");
        if (board->ensure_network(config.device_id) < 0) {
            ESP_LOGE(TAG, "event=control state=waiting_network elapsed_ms=%" PRId64 " result=error",
                     (esp_timer_get_time() - network_started_us) / 1000);
            break;
        }
        int64_t mybot_started_us = esp_timer_get_time();
        ESP_LOGI(TAG, "event=control state=starting_mybot network_elapsed_ms=%" PRId64,
                 (mybot_started_us - network_started_us) / 1000);
        if (mybot_start(&config) < 0) {

            ESP_LOGE(TAG, "event=control state=starting_mybot elapsed_ms=%" PRId64 " result=error",
                     (esp_timer_get_time() - mybot_started_us) / 1000);
            break;
        }

        ESP_LOGI(TAG, "event=control state=mybot_running start_elapsed_ms=%" PRId64 " device=%s",
                 (esp_timer_get_time() - mybot_started_us) / 1000, config.device_id);

        bool provisioning_requested = false;
        while (mybot_is_running()) {

            if (mybot_board_wait_wifi_provisioning_request(CONTROL_EVENT_WAIT_MS)) {
                provisioning_requested = true;
                break;
            }
        }

        if (!provisioning_requested) {
            provisioning_requested = mybot_board_wait_wifi_provisioning_request(0);
        }
        int64_t stop_started_us = esp_timer_get_time();
        ESP_LOGI(TAG, "event=control state=stopping_mybot sdk_state=%d running=%d reason=%s",
                 (int)mybot_get_state(), mybot_is_running() ? 1 : 0,
                 provisioning_requested ? "provision_request" : "sdk_stopped");
        if (stop_mybot_bounded() < 0) {
            ESP_LOGE(TAG, "event=control state=stop_incomplete action=halt "
                          "provisioning=blocked network_shutdown=blocked");
            return;
        }

        if (!provisioning_requested) {
            provisioning_requested = mybot_board_wait_wifi_provisioning_request(0);
        }

        if (!provisioning_requested) {
            ESP_LOGI(TAG,
                     "event=control state=stopped stop_elapsed_ms=%" PRId64 " reason=sdk_stopped",
                     (esp_timer_get_time() - stop_started_us) / 1000);
            break;
        }
        int64_t provision_started_us = esp_timer_get_time();
        ESP_LOGI(TAG, "event=control state=provisioning stop_elapsed_ms=%" PRId64 " source=button",
                 (provision_started_us - stop_started_us) / 1000);
        if (board->provision_wifi() < 0) {
            ESP_LOGE(TAG, "event=control state=provisioning elapsed_ms=%" PRId64 " result=error",
                     (esp_timer_get_time() - provision_started_us) / 1000);
            break;
        }
        (void)mybot_board_wait_wifi_provisioning_request(0);
    }

    ESP_LOGI(TAG, "event=control state=shutting_down");
    board->shutdown_network();
    mybot_time_sync_stop();
}
