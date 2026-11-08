/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_MOSAICO_KEYS_H_
#define MYBOT_MOSAICO_KEYS_H_

#include <mybot/platform/mybot_key.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Board-owned physical button source; shared routing lives in input/ui_input.c. */
int mybot_mosaico_keys_start(void);
const mybot_key_ops_t *mybot_mosaico_key_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_MOSAICO_KEYS_H_ */
