/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_PLATFORM_WIFI_CONTROL_H_
#define MYBOT_PLATFORM_WIFI_CONTROL_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mybot_wifi_provisioning_handler_t)(void);

#define MYBOT_WIFI_PROVISIONING_SSID_CAPACITY 33

/* CRC-16/CCITT-FALSE of all six STA MAC bytes, used for the four-hex AP suffix.
 * Stable for the same MAC; the 16-bit name space does not guarantee uniqueness. */
uint16_t mybot_wifi_provisioning_id(const uint8_t mac[6]);

int mybot_wifi_ensure_network(const char *device_id,
                              mybot_wifi_provisioning_handler_t on_provisioning);
int mybot_wifi_run_provisioning(mybot_wifi_provisioning_handler_t on_provisioning);
/* Copies the active provisioning AP name, including its NUL terminator.
 * Returns -1 if unavailable or capacity is insufficient; a nonempty output
 * buffer is cleared on failure. Never returns a partial SSID or a borrowed pointer. */
int mybot_wifi_get_provisioning_ssid(char *ssid, size_t capacity);
/* Current station connectivity, independent of the SDK workflow screen. */
bool mybot_wifi_is_connected(void);
void mybot_wifi_shutdown_network(void);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_PLATFORM_WIFI_CONTROL_H_ */
