/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_RTC_ATOMIC_H
#define TEST_RTC_ATOMIC_H
#include <stdint.h>
typedef intptr_t aosl_atomic_t;
static inline intptr_t aosl_atomic_read(const aosl_atomic_t *value) {
    return *value;
}
static inline void aosl_atomic_set(aosl_atomic_t *value, intptr_t next) {
    *value = next;
}
static inline intptr_t aosl_atomic_cmpxchg(aosl_atomic_t *value, intptr_t old, intptr_t next) {
    intptr_t previous = *value;
    if (previous == old)
        *value = next;
    return previous;
}
#endif
