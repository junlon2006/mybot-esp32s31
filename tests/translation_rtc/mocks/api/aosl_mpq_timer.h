/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_RTC_MPQ_TIMER_H
#define TEST_RTC_MPQ_TIMER_H
#include <api/aosl_mpq.h>
typedef int aosl_timer_t;
typedef void (*aosl_obj_dtor_t)(uintptr_t argc, uintptr_t argv[]);
typedef void (*aosl_timer_func_t)(aosl_timer_t, const aosl_ts_t *, uintptr_t, uintptr_t[]);
#define AOSL_MPQ_TIMER_INVALID ((aosl_timer_t) - 1)
#define aosl_mpq_timer_invalid(timer) ((timer) < 0)
aosl_timer_t aosl_mpq_set_timer(uintptr_t interval, aosl_timer_func_t callback,
                                aosl_obj_dtor_t destructor, uintptr_t argc, ...);
int aosl_mpq_kill_timer(aosl_timer_t timer);
#endif
