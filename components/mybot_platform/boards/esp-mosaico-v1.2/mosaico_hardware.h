/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_MOSAICO_HARDWARE_H_
#define MYBOT_MOSAICO_HARDWARE_H_

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Validate the eFuse board map before driving pins, then prepare board-owned
 * power/I2C resources. V1.0 is rejected; unknown revisions warn and assume V1.2. */
int mosaico_hardware_init(void);
/* Shared bus ownership remains with the board for the whole boot. */
i2c_master_bus_handle_t mosaico_hardware_i2c_bus(void);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_MOSAICO_HARDWARE_H_ */
