/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_CONTROL_TIMER_H
#define TEST_CONTROL_TIMER_H
#include <api/aosl_mpq.h>
typedef int aosl_timer_t;
#define AOSL_MPQ_TIMER_INVALID (-1)
#define aosl_mpq_timer_invalid(timer) ((timer) < 0)
typedef void (*aosl_timer_func_t)(aosl_timer_t, const aosl_ts_t *, uintptr_t, uintptr_t[]);
aosl_timer_t aosl_mpq_set_timer(uintptr_t interval, aosl_timer_func_t callback, void *destructor,
                                uintptr_t argc, ...);
int aosl_mpq_kill_timer(aosl_timer_t timer);
#endif
