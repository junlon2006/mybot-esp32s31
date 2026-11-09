/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_HOST_AOSL_LOG_H
#define MYBOT_HOST_AOSL_LOG_H

void test_log(const char *format, ...);

#define AOSL_LOG_NTC(...) test_log(__VA_ARGS__)
#define AOSL_LOG_ERR(...) test_log(__VA_ARGS__)
#define AOSL_LOG_DBG(...) test_log(__VA_ARGS__)
#define AOSL_LOG_WRN(...) test_log(__VA_ARGS__)

int aosl_get_log_level(void);
int aosl_set_log_level(int level);

#endif
