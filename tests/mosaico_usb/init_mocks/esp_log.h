/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

void test_usb_init_log(const char *tag, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
#define ESP_LOGI test_usb_init_log
#define ESP_LOGW test_usb_init_log
