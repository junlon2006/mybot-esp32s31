/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdarg.h>
static inline void host_lifecycle_log(const char *format, ...) {
    (void)format;
}
#define AOSL_LOG_ERR(...) host_lifecycle_log(__VA_ARGS__)
#define AOSL_LOG_WRN(...) host_lifecycle_log(__VA_ARGS__)
#define AOSL_LOG_NTC(...) host_lifecycle_log(__VA_ARGS__)
#define AOSL_LOG_DBG(...) host_lifecycle_log(__VA_ARGS__)
