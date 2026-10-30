/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_platform/board.h"
#include <mybot/platform/mybot_platform.h>
#include "board_config.h"
#include "korvo_s31_hardware.h"
#include "announcement/embedded_pcm_prompt.h"
#include "network/wifi_control.h"
#include "esp_log.h"

const mybot_audio_capture_ops_t *mybot_korvo_s31_audio_capture_ops(void);
const mybot_audio_playback_ops_t *mybot_korvo_s31_audio_playback_ops(void);
const mybot_audio_volume_ops_t *mybot_korvo_s31_audio_volume_ops(void);
const mybot_key_ops_t *mybot_korvo_s31_key_ops(void);
const mybot_lcd_ops_t *mybot_korvo_s31_lcd_ops(void);
const mybot_announce_ops_t *mybot_korvo_s31_announce_ops(void);
const mybot_https_ops_t *mybot_esp_idf_https_ops(void);
const mybot_kv_store_ops_t *mybot_esp_idf_kv_store_ops(void);
const mybot_wifi_ops_t *mybot_esp_idf_wifi_ops(void);
int mybot_korvo_s31_keys_start(void);

static const mybot_lcd_ops_t *s_lcd;
static void *s_display;
static bool s_automatic_provisioning;
static bool s_prepared;
static int show(mybot_lcd_screen_t screen) {
    const mybot_lcd_content_t content = {.screen = screen};
    return s_lcd && s_display ? s_lcd->render(s_display, &content) : -1;
}
static void provisioning(void) {
    (void)show(MYBOT_LCD_SCREEN_WIFI_PROVISIONING);
    (void)mybot_embedded_pcm_play_wifi_provisioning(mybot_korvo_s31_audio_playback_ops(),
                                                    mybot_korvo_s31_audio_volume_ops(), "board");
}
static void automatic_provisioning(void) {
    s_automatic_provisioning = true;
    provisioning();
}
static int prepare(void) {
    if (s_prepared)
        return 0;
    if (korvo_s31_hardware_init() < 0)
        return -1;
    s_lcd = mybot_korvo_s31_lcd_ops();
    if (!s_lcd || s_lcd->init(&s_display) < 0)
        return -1;
    if (show(MYBOT_LCD_SCREEN_STARTING) < 0 || mybot_korvo_s31_keys_start() < 0) {
        s_lcd->destroy(s_display);
        s_display = NULL;
        return -1;
    }
    ESP_LOGI("korvo_s31", "event=board_ready audio_bus=tdm video=disabled");
    s_prepared = true;
    return 0;
}
static int register_platform(void) {
    const mybot_platform_descriptor_t descriptor = {
        .wifi = mybot_esp_idf_wifi_ops(),
        .kv_store = mybot_esp_idf_kv_store_ops(),
        .key = mybot_korvo_s31_key_ops(),
        .audio_capture = mybot_korvo_s31_audio_capture_ops(),
        .audio_playback = mybot_korvo_s31_audio_playback_ops(),
        .audio_volume = mybot_korvo_s31_audio_volume_ops(),
        .announce = mybot_korvo_s31_announce_ops(),
        .https = mybot_esp_idf_https_ops(),
        .lcd = mybot_korvo_s31_lcd_ops(),
    };
    return mybot_platform_register(&descriptor);
}
static int ensure_network(const char *device_id) {
    s_automatic_provisioning = false;
    int result = mybot_wifi_ensure_network(device_id, automatic_provisioning);
    if (s_automatic_provisioning)
        (void)mybot_board_wait_wifi_provisioning_request(0);
    return result;
}
static int provision_wifi(void) {
    return mybot_wifi_run_provisioning(provisioning);
}
static const mybot_board_t s_board = {
    .id = MYBOT_BOARD_NAME,
    .hw_model = MYBOT_BOARD_NAME,
    .prepare = prepare,
    .register_platform = register_platform,
    .ensure_network = ensure_network,
    .provision_wifi = provision_wifi,
    .shutdown_network = mybot_wifi_shutdown_network,
};
const mybot_board_t *mybot_board_get(void) {
    return &s_board;
}
