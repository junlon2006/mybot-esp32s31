/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_TRANSLATION_LIFECYCLE_MOCKS_API_AOSL_LOG_H_
#define MYBOT_TEST_TRANSLATION_LIFECYCLE_MOCKS_API_AOSL_LOG_H_

#include <stdarg.h>
static inline void host_lifecycle_log(const char *format, ...) {
    (void)format;
}
#define AOSL_LOG_ERR(...) host_lifecycle_log(__VA_ARGS__)
#define AOSL_LOG_WRN(...) host_lifecycle_log(__VA_ARGS__)
#define AOSL_LOG_NTC(...) host_lifecycle_log(__VA_ARGS__)
#define AOSL_LOG_DBG(...) host_lifecycle_log(__VA_ARGS__)

#endif /* MYBOT_TEST_TRANSLATION_LIFECYCLE_MOCKS_API_AOSL_LOG_H_ */
