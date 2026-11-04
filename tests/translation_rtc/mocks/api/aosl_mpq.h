/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_RTC_MPQ_H
#define TEST_RTC_MPQ_H
#include <stdint.h>
#include <api/aosl_time.h>
typedef intptr_t aosl_mpq_t;
typedef void *aosl_refobj_t;
typedef intptr_t aosl_ref_t;
typedef void (*aosl_mpq_func_t)(const aosl_ts_t *, aosl_refobj_t, uintptr_t, uintptr_t[]);
#define AOSL_MPQ_INVALID ((aosl_mpq_t) - 1)
#define AOSL_REF_INVALID ((aosl_ref_t) - 1)
#define AOSL_MPQ_FLAG_NONBLOCK 1
#define AOSL_MPQ_FLAG_SIGP_EVENT 2
#define AOSL_THRD_PRI_NORMAL 2
#define aosl_mpq_invalid(q) ((q) < 0)
int aosl_mpq_queue(aosl_mpq_t target, aosl_mpq_t source, aosl_ref_t ref, const char *name,
                   aosl_mpq_func_t callback, uintptr_t argc, ...);
int aosl_mpq_call_argv(aosl_mpq_t queue, aosl_ref_t ref, const char *name, aosl_mpq_func_t callback,
                       uintptr_t argc, uintptr_t argv[]);
aosl_mpq_t aosl_mpq_create_flags(int flags, int priority, int stack, int maximum, const char *name,
                                 void *init, void *fini, void *argument);
int aosl_mpq_destroy_wait(aosl_mpq_t queue);
#endif
