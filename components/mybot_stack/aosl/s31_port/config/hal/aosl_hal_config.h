/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_S31_AOSL_HAL_CONFIG_H
#define MYBOT_S31_AOSL_HAL_CONFIG_H

/* ESP-IDF provides select(), pthread condition variables and the hardware RNG. */
#define AOSL_HAL_HAVE_EPOLL 0
#define AOSL_HAL_HAVE_POLL 0
#define AOSL_HAL_HAVE_SELECT 1
#define AOSL_HAL_HAVE_COND 1
#define AOSL_HAL_HAVE_SEM 0
#define AOSL_HAL_HAVE_HWRNG 1

#endif /* MYBOT_S31_AOSL_HAL_CONFIG_H */
