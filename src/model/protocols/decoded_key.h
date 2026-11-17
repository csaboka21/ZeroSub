/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace model {

/* A fixed-code remote key. `protocol` uses the names found in Flipper Zero .sub files. */
struct DecodedKey {
    const char* protocol{""};
    uint32_t bits{0};
    uint64_t key{0};
    /* Base pulse length in microseconds: measured where possible, otherwise nominal. */
    uint32_t te_us{0};
    /*
     * A few frames are longer than 64 bits. `key` then holds the leading bits
     * and `extra` the `extra_bits` that follow, so nothing received is lost.
     * `bits` is the length of the whole frame.
     */
    uint64_t extra{0};
    uint32_t extra_bits{0};

    bool same_key(const DecodedKey& other) const;
};

} // namespace model
