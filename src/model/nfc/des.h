/*
 * SPDX-License-Identifier: MIT
 *
 * Two-key triple DES (EDE) in CBC mode, as MIFARE Ultralight C uses it for
 * its mutual authentication. Small and slow; it runs on a few blocks per
 * authentication only.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace model {

/* `key` is 16 bytes: K1 then K2. `iv` is 8 bytes. `size` is a multiple of 8; in and out may be the same. */
void des3_cbc_encrypt(const uint8_t* key, const uint8_t* iv, const uint8_t* in, std::size_t size, uint8_t* out);
void des3_cbc_decrypt(const uint8_t* key, const uint8_t* iv, const uint8_t* in, std::size_t size, uint8_t* out);

} // namespace model
