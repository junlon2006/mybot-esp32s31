/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_TRANSLATION_LIFECYCLE_MOCKS_API_AOSL_ATOMIC_H_
#define MYBOT_TEST_TRANSLATION_LIFECYCLE_MOCKS_API_AOSL_ATOMIC_H_

#include <stdint.h>
typedef intptr_t aosl_atomic_t;
static inline intptr_t aosl_atomic_read(const aosl_atomic_t *v) {
    return __atomic_load_n(v, __ATOMIC_SEQ_CST);
}
static inline void aosl_atomic_set(aosl_atomic_t *v, intptr_t value) {
    __atomic_store_n(v, value, __ATOMIC_SEQ_CST);
}
static inline intptr_t aosl_atomic_xchg(aosl_atomic_t *v, intptr_t value) {
    return __atomic_exchange_n(v, value, __ATOMIC_SEQ_CST);
}
static inline void aosl_atomic_inc(aosl_atomic_t *v) {
    (void)__atomic_add_fetch(v, 1, __ATOMIC_SEQ_CST);
}

#endif /* MYBOT_TEST_TRANSLATION_LIFECYCLE_MOCKS_API_AOSL_ATOMIC_H_ */
