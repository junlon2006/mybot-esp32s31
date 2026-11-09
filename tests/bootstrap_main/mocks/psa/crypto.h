/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Only bootstrap control flow is under test, not PSA or the device-ID digest. */
typedef int psa_key_attributes_t;
typedef int psa_key_id_t;
typedef int psa_status_t;

#define PSA_KEY_ATTRIBUTES_INIT 0
#define PSA_KEY_TYPE_HMAC 1
#define PSA_ALG_SHA_256 2
#define PSA_ALG_HMAC(algorithm) (algorithm)
#define PSA_KEY_USAGE_SIGN_MESSAGE 4
#define PSA_SUCCESS 0

void psa_set_key_type(psa_key_attributes_t *attributes, int type);
void psa_set_key_algorithm(psa_key_attributes_t *attributes, int algorithm);
void psa_set_key_usage_flags(psa_key_attributes_t *attributes, int usage);
psa_status_t psa_import_key(const psa_key_attributes_t *attributes, const uint8_t *data,
                            size_t data_length, psa_key_id_t *key);
void psa_reset_key_attributes(psa_key_attributes_t *attributes);
psa_status_t psa_mac_compute(psa_key_id_t key, int algorithm, const uint8_t *input,
                             size_t input_length, uint8_t *mac, size_t mac_size,
                             size_t *mac_length);
psa_status_t psa_destroy_key(psa_key_id_t key);
