/* SPDX-License-Identifier: Apache-2.0 */
#include "ui/ui_port.h"

#include "input/ui_input.h"
#include "network/wifi_control.h"

_Static_assert(MYBOT_UI_PORT_SSID_CAPACITY == MYBOT_WIFI_PROVISIONING_SSID_CAPACITY,
               "UI and Wi-Fi provisioning SSID capacities must match");

const mybot_ui_language_t *mybot_ui_port_languages(size_t *count) {
    return mybot_ui_languages(count);
}

int mybot_ui_port_copy_caption(uint32_t generation, uint32_t sequence, mybot_caption_t *out) {
    return mybot_ui_copy_caption(generation, sequence, out);
}

bool mybot_ui_port_wifi_connected(void) {
    return mybot_wifi_is_connected();
}

int mybot_ui_port_provisioning_ssid(char *ssid, size_t capacity) {
    return mybot_wifi_get_provisioning_ssid(ssid, capacity);
}

int mybot_ui_port_submit(const mybot_ui_command_t *command) {
    return mybot_ui_submit(command);
}

void mybot_ui_port_gesture_command(const mybot_ui_command_t *command) {
    mybot_ui_input_command(command);
}

void mybot_ui_port_event(mybot_key_event_t event) {
    mybot_ui_input_event(event);
}
