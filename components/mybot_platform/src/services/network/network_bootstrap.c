/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_platform/network.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"

#include <inttypes.h>
#include <stddef.h>

#define TAG "mybot_network"

int mybot_network_bootstrap(void) {
    /* AOSL creates socket-backed wakeup pipes before the board Wi-Fi callback runs. */
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif initialization failed: %s", esp_err_to_name(err));
        return -1;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "default event loop initialization failed: %s", esp_err_to_name(err));
        return -1;
    }
    return 0;
}

static esp_event_handler_instance_t s_got_ip_handler;
static bool s_sntp_initialized;

static void on_time_sync(struct timeval *tv) {
    (void)tv;
    ESP_LOGI(TAG, "system time synchronized via SNTP epoch=%" PRId64, (int64_t)tv->tv_sec);
}

static void start_sntp_on_got_ip(void *arg, esp_event_base_t event_base, int32_t event_id,
                                 void *event_data) {
    (void)arg;
    (void)event_base;
    (void)event_id;
    (void)event_data;

    esp_err_t err = esp_netif_sntp_start();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SNTP time sync started");
    } else {
        ESP_LOGE(TAG, "failed to start SNTP: %s", esp_err_to_name(err));
    }
}

int mybot_time_sync_start(void) {
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "time.cloudflare.com", "pool.ntp.org"));
    config.start = false;
    config.wait_for_sync = false;
    config.sync_cb = on_time_sync;

    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SNTP initialization failed: %s", esp_err_to_name(err));
        return -1;
    }

    /* Register before Wi-Fi so time sync starts before usable connectivity is published. */
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, start_sntp_on_got_ip,
                                              NULL, &s_got_ip_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SNTP IP handler registration failed: %s", esp_err_to_name(err));
        esp_netif_sntp_deinit();
        return -1;
    }
    s_sntp_initialized = true;
    ESP_LOGI(TAG, "event=time_sync source=sntp state=configured servers=%u",
             (unsigned int)config.num_of_servers);
    return 0;
}

void mybot_time_sync_stop(void) {
    if (!s_sntp_initialized) {
        return;
    }
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_got_ip_handler);
    esp_netif_sntp_deinit();
    s_sntp_initialized = false;
}
