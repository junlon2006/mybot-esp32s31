/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_RESOURCE_MONITOR_H_
#define MYBOT_RESOURCE_MONITOR_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Start once from app_main; the diagnostic task lives until reboot. */
int mybot_resource_monitor_start(void);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_RESOURCE_MONITOR_H_ */
