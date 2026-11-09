/* SPDX-License-Identifier: Apache-2.0 */
#include <mybot/mybot.h>
#include <mybot/mybot_version.h>
#include <agora_rtc_api.h>
#include "mybot_platform/board.h"
#include "mybot_platform/factory_reset.h"
#include "mybot_platform/network.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "psa/crypto.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

void app_main(void);

typedef struct {
    const char *name;
    bool start_fails;
    unsigned stop_failures;
    bool request_provisioning;
    unsigned expected_stops;
    unsigned expected_provisions;
    unsigned expected_shutdowns;
} test_case_t;

static const test_case_t *active_case;
static unsigned starts, stops, provisions, shutdowns, ensures, delays, sync_stops;
static bool sdk_retained;

void test_bootstrap_log(const char *tag, const char *format, ...) {
    (void)tag;
    (void)format;
}

const char *mybot_version_string(void) {
    return "host-test";
}

const char *agora_rtc_get_version(void) {
    return "host-test";
}

const char *esp_get_idf_version(void) {
    return "host-test";
}

const esp_app_desc_t *esp_app_get_description(void) {
    static const esp_app_desc_t description = {"date", "time", "host-test"};
    return &description;
}

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type) {
    assert(type == ESP_MAC_WIFI_STA);
    memset(mac, 0, 6);
    return ESP_OK;
}

bool esp_psram_is_initialized(void) {
    return true;
}

int64_t esp_timer_get_time(void) {
    static int64_t now;
    return ++now;
}

bool mybot_factory_reset_handle_pending(void) {
    return false;
}

int mybot_storage_init(void) {
    return 0;
}

int mybot_network_bootstrap(void) {
    return 0;
}

int mybot_time_sync_start(void) {
    return 0;
}

void mybot_time_sync_stop(void) {
    assert(!sdk_retained);
    sync_stops++;
}

int mybot_board_register(void) {
    return 0;
}

static int ensure_network(const char *device_id) {
    assert(device_id[0] && !sdk_retained);
    /* A successful reprovision reaches the next loop. End there, before a new SDK start. */
    return ++ensures == 1 ? 0 : -1;
}

static int provision_wifi(void) {
    assert(!sdk_retained && !active_case->start_fails);
    provisions++;
    return 0;
}

static void shutdown_network(void) {
    assert(!sdk_retained);
    shutdowns++;
}

const mybot_board_t *mybot_board_get(void) {
    static const mybot_board_t board = {.hw_model = "host-test",
                                        .ensure_network = ensure_network,
                                        .provision_wifi = provision_wifi,
                                        .shutdown_network = shutdown_network};
    return &board;
}

bool mybot_board_wait_wifi_provisioning_request(uint32_t timeout_ms) {
    (void)timeout_ms;
    return active_case->request_provisioning && !provisions;
}

int mybot_start(const mybot_config_t *config) {
    assert(!sdk_retained && config->device_id[0]);
    assert(strcmp(config->server_base, "https://example.invalid/api") == 0);
    starts++;
    /* A synchronous failure may still own workers after its initial cleanup attempt. */
    sdk_retained = true;
    return active_case->start_fails ? -1 : 0;
}

bool mybot_is_running(void) {
    return active_case->request_provisioning && !provisions;
}

mybot_state_t mybot_get_state(void) {
    return sdk_retained ? MYBOT_STATE_STOPPING : MYBOT_STATE_STOPPED;
}

int mybot_stop(void) {
    assert(sdk_retained);
    if (++stops <= active_case->stop_failures)
        return -1;
    sdk_retained = false;
    return 0;
}

void vTaskDelay(TickType_t ticks) {
    assert(ticks > 0 && sdk_retained);
    delays++;
}

void psa_set_key_type(psa_key_attributes_t *attributes, int type) {
    (void)attributes;
    assert(type == PSA_KEY_TYPE_HMAC);
}

void psa_set_key_algorithm(psa_key_attributes_t *attributes, int algorithm) {
    (void)attributes;
    assert(algorithm == PSA_ALG_HMAC(PSA_ALG_SHA_256));
}

void psa_set_key_usage_flags(psa_key_attributes_t *attributes, int usage) {
    (void)attributes;
    assert(usage == PSA_KEY_USAGE_SIGN_MESSAGE);
}

psa_status_t psa_import_key(const psa_key_attributes_t *attributes, const uint8_t *data,
                            size_t data_length, psa_key_id_t *key) {
    (void)attributes;
    assert(data && data_length > 0);
    *key = 1;
    return PSA_SUCCESS;
}

void psa_reset_key_attributes(psa_key_attributes_t *attributes) {
    *attributes = PSA_KEY_ATTRIBUTES_INIT;
}

psa_status_t psa_mac_compute(psa_key_id_t key, int algorithm, const uint8_t *input,
                             size_t input_length, uint8_t *mac, size_t mac_size,
                             size_t *mac_length) {
    assert(key == 1 && algorithm == PSA_ALG_HMAC(PSA_ALG_SHA_256));
    assert(input && input_length == 6 && mac_size == 32);
    memset(mac, 0, mac_size);
    *mac_length = mac_size;
    return PSA_SUCCESS;
}

psa_status_t psa_destroy_key(psa_key_id_t key) {
    assert(key == 1);
    return PSA_SUCCESS;
}

int main(void) {
    static const test_case_t cases[] = {
        {"failed start, cleanup succeeds", true, 0, false, 1, 0, 1},
        {"failed start, cleanup retries", true, 2, false, 3, 0, 1},
        {"failed start, cleanup retained", true, UINT_MAX, false, 3, 0, 0},
        {"reprovision, cleanup retries", false, 2, true, 3, 1, 1},
        {"reprovision, cleanup retained", false, UINT_MAX, true, 3, 0, 0},
        {"SDK exit, cleanup retries", false, 2, false, 3, 0, 1},
        {"SDK exit, cleanup retained", false, UINT_MAX, false, 3, 0, 0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        active_case = &cases[i];
        starts = stops = provisions = shutdowns = ensures = delays = sync_stops = 0;
        sdk_retained = false;
        app_main();
        assert(starts == 1 && stops == active_case->expected_stops);
        assert(provisions == active_case->expected_provisions);
        assert(shutdowns == active_case->expected_shutdowns && sync_stops == shutdowns);
        assert(sdk_retained == (shutdowns == 0));
        assert(delays == (stops > 1 ? stops - 1 : 0));
        assert(ensures == 1 + provisions);
        printf("PASS bootstrap_main %s\n", active_case->name);
    }
    return 0;
}
