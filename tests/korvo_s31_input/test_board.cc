/* SPDX-License-Identifier: MIT */
#include "host_input.h"
#include "mybot_platform/board.h"
#include "network/wifi_control.h"
#include <mybot/platform/mybot_platform.h>

#ifndef TEST_MOSAICO
#define TEST_MOSAICO 0
#endif
#if TEST_MOSAICO
#include "usb/mosaico_usb_audio.h"
#define korvo_s31_hardware_init mosaico_hardware_init
#define mybot_korvo_s31_keys_start mybot_mosaico_keys_start
#define mybot_korvo_s31_lcd_ops mybot_mosaico_lcd_ops
#define mybot_korvo_s31_audio_capture_ops mybot_mosaico_audio_capture_ops
#define mybot_korvo_s31_audio_playback_ops mybot_mosaico_audio_playback_ops
#define mybot_korvo_s31_audio_volume_ops mybot_mosaico_audio_volume_ops
#define mybot_korvo_s31_key_ops mybot_mosaico_key_ops
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #test);                   \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

static bool hardware_ready, lcd_open, keys_started, automatic;
static unsigned hardware_starts, lcd_starts, key_starts, lcd_destroys;
static unsigned prompt_calls, waits, requests, shutdowns;
static int lcd_context;
static int network_result;
static std::string failure;
#if TEST_MOSAICO
static bool usb_initialized = true, usb_console_error, usb_stream_open = true;
static unsigned usb_initializations;
static mybot_platform_descriptor_t registered;

static int usb_capture_start(void *) {
    return usb_stream_open ? 0 : -1;
}
#endif

static bool fail(const char *name) {
    if (failure != name)
        return false;
    failure.clear();
    return true;
}

static int lcd_init(void **out) {
    CHECK(hardware_ready && !lcd_open);
    ++lcd_starts;
    *out = nullptr;
    if (fail("lcd_init"))
        return -1;
    lcd_open = true;
    *out = &lcd_context;
    return 0;
}
static int lcd_render(void *ctx, const mybot_lcd_content_t *content) {
    CHECK(ctx == &lcd_context && lcd_open && content);
    CHECK(content->screen == MYBOT_LCD_SCREEN_STARTING ||
          content->screen == MYBOT_LCD_SCREEN_WIFI_PROVISIONING);
    return fail("lcd_render") ? -1 : 0;
}
static void lcd_destroy(void *ctx) {
    CHECK(ctx == &lcd_context && lcd_open);
    lcd_open = false;
    ++lcd_destroys;
}
static const mybot_lcd_ops_t lcd_ops{lcd_init, lcd_render, lcd_destroy};

extern "C" {
int korvo_s31_hardware_init() {
    ++hardware_starts;
    if (fail("hardware"))
        return -1;
    hardware_ready = true;
    return 0;
}
int mybot_korvo_s31_keys_start() {
    CHECK(lcd_open);
    ++key_starts;
    if (fail("keys"))
        return -1;
    keys_started = true;
    return 0;
}
const mybot_lcd_ops_t *mybot_korvo_s31_lcd_ops() {
    return &lcd_ops;
}
#define MOCK_OPS(name, type)                                                                       \
    const type *name() {                                                                           \
        static const type ops{};                                                                   \
        return &ops;                                                                               \
    }
MOCK_OPS(mybot_korvo_s31_audio_capture_ops, mybot_audio_capture_ops_t)
MOCK_OPS(mybot_korvo_s31_audio_playback_ops, mybot_audio_playback_ops_t)
MOCK_OPS(mybot_korvo_s31_audio_volume_ops, mybot_audio_volume_ops_t)
MOCK_OPS(mybot_korvo_s31_key_ops, mybot_key_ops_t)
MOCK_OPS(mybot_korvo_s31_announce_ops, mybot_announce_ops_t)
MOCK_OPS(mybot_esp_idf_https_ops, mybot_https_ops_t)
MOCK_OPS(mybot_esp_idf_kv_store_ops, mybot_kv_store_ops_t)
MOCK_OPS(mybot_esp_idf_wifi_ops, mybot_wifi_ops_t)
#if TEST_MOSAICO
esp_err_t mosaico_usb_console_init() {
    CHECK(hardware_ready);
    ++usb_initializations;
    return usb_console_error ? ESP_FAIL : ESP_OK;
}
bool mosaico_usb_audio_initialized() {
    return usb_initialized;
}
const mybot_audio_capture_ops_t *mosaico_usb_audio_capture_ops() {
    CHECK(usb_initialized);
    static const mybot_audio_capture_ops_t ops{nullptr, usb_capture_start, nullptr, nullptr,
                                               nullptr};
    return &ops;
}
MOCK_OPS(mosaico_usb_audio_playback_ops, mybot_audio_playback_ops_t)
#endif
int mybot_platform_register(const mybot_platform_descriptor_t *descriptor) {
    CHECK(hardware_ready && lcd_open && keys_started && descriptor);
    CHECK(descriptor->lcd == &lcd_ops && !descriptor->video);
    CHECK(descriptor->wifi && descriptor->kv_store && descriptor->key && descriptor->announce);
    CHECK(descriptor->audio_capture && descriptor->audio_playback && descriptor->audio_volume &&
          descriptor->https);
#if TEST_MOSAICO
    if (usb_initialized) {
        CHECK(descriptor->audio_usb_capture == mosaico_usb_audio_capture_ops() &&
              descriptor->audio_usb_playback == mosaico_usb_audio_playback_ops());
    } else {
        CHECK(!descriptor->audio_usb_capture && !descriptor->audio_usb_playback);
    }
    registered = *descriptor;
#else
    CHECK(!descriptor->audio_usb_capture && !descriptor->audio_usb_playback);
#endif
    return fail("register") ? -1 : 0;
}
int mybot_embedded_pcm_play_wifi_provisioning(const mybot_audio_playback_ops_t *playback,
                                              const mybot_audio_volume_ops_t *volume,
                                              const char *trigger) {
    CHECK(playback == mybot_korvo_s31_audio_playback_ops() &&
          volume == mybot_korvo_s31_audio_volume_ops());
    CHECK(trigger && !std::strcmp(trigger, "board"));
    ++prompt_calls;
    return 0;
}
int mybot_wifi_ensure_network(const char *id, mybot_wifi_provisioning_handler_t on_provisioning) {
    CHECK(lcd_open && keys_started && id && !std::strcmp(id, "host-test") && on_provisioning);
    if (automatic)
        on_provisioning();
    return network_result;
}
int mybot_wifi_run_provisioning(mybot_wifi_provisioning_handler_t on_provisioning) {
    CHECK(lcd_open && on_provisioning);
    on_provisioning();
    return network_result;
}
void mybot_wifi_shutdown_network() {
    ++shutdowns;
}
void mybot_board_request_wifi_provisioning() {
    ++requests;
}
bool mybot_board_wait_wifi_provisioning_request(uint32_t timeout) {
    CHECK(timeout == 0);
    ++waits;
    return false;
}
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    const std::string scenario = argv[1];
#if TEST_MOSAICO
    const bool usb_scenario = scenario == "usb_unavailable" || scenario == "usb_console_error" ||
                              scenario == "usb_no_stream";
    usb_initialized = scenario != "usb_unavailable";
    usb_console_error = scenario == "usb_unavailable" || scenario == "usb_console_error";
    usb_stream_open = scenario != "usb_no_stream";
#else
    const bool usb_scenario = false;
#endif
    const mybot_board_t *board = mybot_board_get();
    CHECK(board &&
          !std::strcmp(board->id, TEST_MOSAICO ? "esp-mosaico-v1.2" : "esp32-s31-korvo-1") &&
          !std::strcmp(board->hw_model, board->id));
    if (scenario != "normal" && !usb_scenario) {
        failure = scenario;
        CHECK(board->prepare() < 0 && !lcd_open);
        CHECK(hardware_ready == (scenario != "hardware"));
        CHECK(lcd_destroys == (scenario == "keys" || scenario == "lcd_render" ? 1U : 0U));
    }
    CHECK(board->prepare() == 0 && lcd_open && keys_started);
#if TEST_MOSAICO
    const unsigned usb_before = usb_initializations;
#endif
    const unsigned hardware_before = hardware_starts, lcd_before = lcd_starts,
                   keys_before = key_starts;
    for (unsigned i = 0; i < 4; ++i)
        CHECK(board->prepare() == 0);
    CHECK(hardware_starts == hardware_before && lcd_starts == lcd_before &&
          key_starts == keys_before);
#if TEST_MOSAICO
    CHECK(usb_initializations == usb_before);
#endif
    CHECK(board->register_platform() == 0);
#if TEST_MOSAICO
    if (scenario == "usb_no_stream") {
        CHECK(registered.audio_usb_capture && registered.audio_usb_playback);
        CHECK(registered.audio_usb_capture->start(nullptr) < 0);
        usb_stream_open = true;
        CHECK(board->register_platform() == 0);
        CHECK(registered.audio_usb_capture && registered.audio_usb_playback);
        CHECK(registered.audio_usb_capture->start(nullptr) == 0);
    }
#endif
    failure = "register";
    CHECK(board->register_platform() < 0);
    CHECK(board->ensure_network("host-test") == 0 && !waits && !prompt_calls);
    automatic = true;
    CHECK(board->ensure_network("host-test") == 0 && waits == 1 && prompt_calls == 1);
    automatic = false;
    CHECK(board->ensure_network("host-test") == 0 && waits == 1);
    network_result = -1;
    CHECK(board->ensure_network("host-test") < 0);
    CHECK(board->provision_wifi() < 0 && prompt_calls == 2);
    network_result = 0;
    CHECK(board->provision_wifi() == 0 && prompt_calls == 3);
    board->shutdown_network();
    CHECK(shutdowns == 1 && lcd_open && keys_started && hardware_ready);
    std::printf("PASS %s_board %s\n", TEST_MOSAICO ? "mosaico" : "korvo_s31", argv[1]);
}
