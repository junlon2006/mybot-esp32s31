/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_STORAGE_INIT_H_
#define MYBOT_STORAGE_INIT_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize persistent storage without erasing credentials or session journals.
 * Return -1 on any failure; explicit recovery belongs to the device owner. */
int mybot_storage_init(void);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_STORAGE_INIT_H_ */
