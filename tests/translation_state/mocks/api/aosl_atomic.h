/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_TRANSLATION_STATE_ATOMIC_H
#define TEST_TRANSLATION_STATE_ATOMIC_H
#include <stdint.h>
typedef intptr_t aosl_atomic_t;
static inline intptr_t aosl_atomic_read(const aosl_atomic_t *value) {
    return *value;
}
static inline void aosl_atomic_set(aosl_atomic_t *value, intptr_t next) {
    *value = next;
}
#endif
