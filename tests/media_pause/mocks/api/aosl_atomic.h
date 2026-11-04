/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_MEDIA_PAUSE_ATOMIC_H
#define TEST_MEDIA_PAUSE_ATOMIC_H
#include <stdint.h>
typedef intptr_t aosl_atomic_t;
void test_atomic_cmpxchg_hook(aosl_atomic_t *value, intptr_t previous, intptr_t next);
static inline intptr_t aosl_atomic_read(const aosl_atomic_t *value) {
    return __atomic_load_n(value, __ATOMIC_SEQ_CST);
}
static inline void aosl_atomic_set(aosl_atomic_t *value, intptr_t next) {
    __atomic_store_n(value, next, __ATOMIC_SEQ_CST);
}
static inline intptr_t aosl_atomic_cmpxchg(aosl_atomic_t *value, intptr_t old, intptr_t next) {
    __atomic_compare_exchange_n(value, &old, next, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    test_atomic_cmpxchg_hook(value, old, next);
    return old;
}
static inline void aosl_atomic_inc(aosl_atomic_t *value) {
    (void)__atomic_fetch_add(value, 1, __ATOMIC_SEQ_CST);
}
static inline void aosl_rmb(void) {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}
static inline void aosl_wmb(void) {
    __atomic_thread_fence(__ATOMIC_RELEASE);
}
#endif
