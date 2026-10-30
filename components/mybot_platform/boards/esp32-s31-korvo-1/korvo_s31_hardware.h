/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "driver/i2c_master.h"
#ifdef __cplusplus
extern "C" {
#endif
int korvo_s31_hardware_init(void);
i2c_master_bus_handle_t korvo_s31_hardware_i2c_bus(void);
#ifdef __cplusplus
}
#endif
