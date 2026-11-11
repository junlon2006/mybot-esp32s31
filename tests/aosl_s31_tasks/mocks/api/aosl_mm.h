/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_AOSL_S31_TASKS_MOCKS_API_AOSL_MM_H_
#define MYBOT_TEST_AOSL_S31_TASKS_MOCKS_API_AOSL_MM_H_

#include <stddef.h>
void *aosl_malloc(size_t);
void *aosl_calloc(size_t, size_t);
void aosl_free(void *);

#endif /* MYBOT_TEST_AOSL_S31_TASKS_MOCKS_API_AOSL_MM_H_ */
