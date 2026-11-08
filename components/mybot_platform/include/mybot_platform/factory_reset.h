/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_PLATFORM_FACTORY_RESET_H_
#define MYBOT_PLATFORM_FACTORY_RESET_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Record a factory-reset request in RTC-retained memory and restart immediately.
 * The request must be completed by mybot_factory_reset_handle_pending() before
 * NVS is initialized on the next boot. */
void mybot_factory_reset_request(void);

/* Erase the default NVS partition when a request is pending, clear the marker and
 * restart. Returns false only when no request is pending. A failed erase retains
 * the marker and restarts to retry; successful completion never returns. */
bool mybot_factory_reset_handle_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_PLATFORM_FACTORY_RESET_H_ */
