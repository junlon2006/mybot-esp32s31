/* SPDX-License-Identifier: Apache-2.0 */
#include "board_config.h"
#include "input/mosaico_keys.h"
#include "input/ui_input.h"

#include "driver/gpio.h"

static int prepare_button(void) {
    const gpio_config_t input = {
        .pin_bit_mask = UINT64_C(1) << MYBOT_AI_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&input) == ESP_OK ? 0 : -1;
}

static bool button_pressed(void) {
    return gpio_get_level(MYBOT_AI_BUTTON_GPIO) == 0;
}

int mybot_mosaico_keys_start(void) {
    return mybot_ui_input_start(prepare_button, button_pressed);
}

static int keys_init(void **out, mybot_key_event_handler_t emit, void *user) {
    return mybot_ui_input_attach(out, emit, user);
}

static void keys_destroy(void *ctx) {
    mybot_ui_input_destroy(ctx);
}

static const mybot_key_ops_t s_keys = {.init = keys_init, .destroy = keys_destroy};
const mybot_key_ops_t *mybot_mosaico_key_ops(void) {
    return &s_keys;
}
