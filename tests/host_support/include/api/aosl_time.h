/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_HOST_AOSL_TIME_H
#define MYBOT_HOST_AOSL_TIME_H

#include <stdint.h>

typedef uint64_t aosl_ts_t;

aosl_ts_t aosl_tick_ms(void);
void aosl_msleep(unsigned milliseconds);

#endif
