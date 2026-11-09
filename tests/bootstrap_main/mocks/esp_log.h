/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

void test_bootstrap_log(const char *tag, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#define ESP_LOGI(...) test_bootstrap_log(__VA_ARGS__)
#define ESP_LOGE(...) test_bootstrap_log(__VA_ARGS__)
