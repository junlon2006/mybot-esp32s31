/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_AOSL_LOG_H
#define TEST_AOSL_LOG_H
void test_client_log(const char *format, ...);
#define AOSL_LOG_NTC(...) test_client_log(__VA_ARGS__)
#define AOSL_LOG_ERR(...) test_client_log(__VA_ARGS__)
#define AOSL_LOG_DBG(...) test_client_log(__VA_ARGS__)
#endif
