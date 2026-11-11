/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_PLATFORM_NETWORK_H
#define MYBOT_PLATFORM_NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the shared ESP netif/event foundation before board Wi-Fi setup. */
int mybot_network_bootstrap(void);

/* Start SNTP before Wi-Fi publishes usable connectivity, and release it at shutdown. */
int mybot_time_sync_start(void);
void mybot_time_sync_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_PLATFORM_NETWORK_H */
