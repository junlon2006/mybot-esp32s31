/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mosaico_local_aec mosaico_local_aec_t;

/* Create an ESP-SR full-duplex AEC instance for 16 kHz mono output. */
mosaico_local_aec_t *mosaico_local_aec_create(void);
void mosaico_local_aec_destroy(mosaico_local_aec_t *aec);

/* Recreate the filter and discard buffered input/output. No other thread may
 * call feed/read concurrently with reset. */
int mosaico_local_aec_reset(mosaico_local_aec_t *aec);

/* Number of interleaved [microphone, DAC reference] frames consumed per call.
 * On success, output frames are appended to the bounded output ring. */
int mosaico_local_aec_chunk_frames(const mosaico_local_aec_t *aec);
int mosaico_local_aec_feed(mosaico_local_aec_t *aec, const int16_t *interleaved, size_t frames);

/* Copy available cleaned mono samples without blocking. */
size_t mosaico_local_aec_read(mosaico_local_aec_t *aec, int16_t *mono, size_t frames);

#ifdef __cplusplus
}
#endif
