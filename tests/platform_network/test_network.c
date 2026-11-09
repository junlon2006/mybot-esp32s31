/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_platform/network.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

esp_event_base_t IP_EVENT = "IP";

static esp_err_t netif_result;
static esp_err_t event_loop_result;
static esp_err_t sntp_init_result;
static esp_err_t register_result;
static esp_err_t start_result;
static unsigned netif_calls;
static unsigned event_loop_calls;
static unsigned sntp_init_calls;
static unsigned register_calls;
static unsigned unregister_calls;
static unsigned sntp_start_calls;
static unsigned sntp_deinit_calls;
static esp_event_handler_t got_ip_handler;
static esp_event_handler_instance_t got_ip_instance;
static esp_sntp_config_t last_sntp_config;

static void reset(void) {
    netif_result = event_loop_result = sntp_init_result = register_result = start_result = ESP_OK;
    netif_calls = event_loop_calls = sntp_init_calls = register_calls = unregister_calls = 0;
    sntp_start_calls = sntp_deinit_calls = 0;
    got_ip_handler = NULL;
    got_ip_instance = NULL;
    memset(&last_sntp_config, 0, sizeof(last_sntp_config));
}

const char *esp_err_to_name(esp_err_t result) {
    return result == ESP_OK ? "OK" : "ERROR";
}

esp_err_t esp_netif_init(void) {
    ++netif_calls;
    return netif_result;
}

esp_err_t esp_event_loop_create_default(void) {
    ++event_loop_calls;
    return event_loop_result;
}

esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id,
                                              esp_event_handler_t handler, void *arg,
                                              esp_event_handler_instance_t *instance) {
    assert(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && arg == NULL && instance != NULL);
    ++register_calls;
    got_ip_handler = handler;
    *instance = &got_ip_instance;
    got_ip_instance = *instance;
    return register_result;
}

esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id,
                                                esp_event_handler_instance_t instance) {
    assert(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && instance == got_ip_instance);
    ++unregister_calls;
    return ESP_OK;
}

esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config) {
    ++sntp_init_calls;
    last_sntp_config = *config;
    return sntp_init_result;
}

esp_err_t esp_netif_sntp_start(void) {
    ++sntp_start_calls;
    return start_result;
}

void esp_netif_sntp_deinit(void) {
    ++sntp_deinit_calls;
}

static void test_network_bootstrap(void) {
    reset();
    assert(mybot_network_bootstrap() == 0);
    assert(netif_calls == 1 && event_loop_calls == 1);

    reset();
    netif_result = event_loop_result = ESP_ERR_INVALID_STATE;
    assert(mybot_network_bootstrap() == 0);

    reset();
    netif_result = ESP_FAIL;
    assert(mybot_network_bootstrap() < 0);
    assert(netif_calls == 1 && event_loop_calls == 0);

    reset();
    event_loop_result = ESP_FAIL;
    assert(mybot_network_bootstrap() < 0);
    assert(netif_calls == 1 && event_loop_calls == 1);
}

static void test_time_sync(void) {
    reset();
    assert(mybot_time_sync_start() == 0);
    assert(sntp_init_calls == 1 && register_calls == 1 && unregister_calls == 0);
    assert(last_sntp_config.start == false && last_sntp_config.wait_for_sync == false);
    assert(last_sntp_config.num_of_servers == 3 && last_sntp_config.servers[0] != NULL);
    assert(last_sntp_config.sync_cb != NULL && got_ip_handler != NULL);
    got_ip_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
    assert(sntp_start_calls == 1);
    mybot_time_sync_stop();
    mybot_time_sync_stop();
    assert(unregister_calls == 1 && sntp_deinit_calls == 1);

    reset();
    sntp_init_result = ESP_FAIL;
    assert(mybot_time_sync_start() < 0);
    assert(register_calls == 0 && sntp_deinit_calls == 0);

    reset();
    register_result = ESP_FAIL;
    assert(mybot_time_sync_start() < 0);
    assert(sntp_deinit_calls == 1);
    mybot_time_sync_stop();
    assert(unregister_calls == 0 && sntp_deinit_calls == 1);

    reset();
    start_result = ESP_FAIL;
    assert(mybot_time_sync_start() == 0);
    got_ip_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
    assert(sntp_start_calls == 1);
    mybot_time_sync_stop();
    assert(sntp_deinit_calls == 1);
}

int main(void) {
    test_network_bootstrap();
    test_time_sync();
    puts("platform network bootstrap and SNTP lifecycle passed");
    return 0;
}
