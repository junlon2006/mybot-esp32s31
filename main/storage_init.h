/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* Initialize persistent storage without erasing credentials or session journals.
 * Return -1 on any failure; explicit recovery belongs to the device owner. */
int mybot_storage_init(void);
