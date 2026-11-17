/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace model {

/*
 * Turns a fixed-code key back into the pulse train its remote sends: the
 * reverse of KeyDecoder. `protocol` is the name used in Flipper Zero .sub files.
 *
 * `pulses` receives signed durations in microseconds, positive for carrier on
 * and negative for carrier off, covering every repeat of the frame that one
 * button press sends. `te_us` is only used by protocols whose pulse length is
 * stored with the key (Princeton, SMC5326); 0 selects their usual value.
 *
 * Returns false with a reason when the protocol or bit count is not supported.
 */
bool encode_key(const std::string& protocol,
                uint32_t bits,
                uint64_t key,
                uint32_t te_us,
                std::vector<int32_t>& pulses,
                std::string& error);

/* The parcel number hidden in a CAME TWEE key selects one of these masks. */
constexpr uint32_t kCameTweeMasks[15] = {
    0x0E0E0E00, 0x1D1D1D11, 0x2C2C2C22, 0x3B3B3B33, 0x4A4A4A44, 0x59595955, 0x68686866, 0x77777777,
    0x86868688, 0x95959599, 0xA4A4A4AA, 0xB3B3B3BB, 0xC2C2C2CC, 0xD1D1D1DD, 0xE0E0E0EE,
};
/* Every CAME TWEE frame carries this value in its upper 22 bits. */
constexpr uint64_t kCameTweePrefix = 0x003FFF72ULL;

} // namespace model
