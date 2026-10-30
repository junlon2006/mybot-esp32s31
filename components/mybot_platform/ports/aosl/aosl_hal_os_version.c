/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>

#include <hal/aosl_hal_utils.h>

int aosl_hal_os_version(char buf[], int buf_sz) {
    if (buf_sz <= 1) {
        return -1;
    }
    snprintf(buf, (size_t)buf_sz, "%s", "esp32-s31");
    return 0;
}
