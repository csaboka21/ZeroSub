/*
 * SPDX-License-Identifier: MIT
 */

#include "transmit_policy.h"

#include <cstdio>

namespace model {
namespace {

struct Band {
    uint32_t first_hz;
    uint32_t last_hz;
};

constexpr Band kEuBands[] = {
    {433050000U, 434790000U},
    {868000000U, 868600000U},
};

} // namespace

bool transmit_allowed(uint32_t frequency_hz, std::string& reason) {
    for (const auto& band : kEuBands) {
        if (frequency_hz >= band.first_hz && frequency_hz <= band.last_hz) {
            reason.clear();
            return true;
        }
    }

    char text[96];
    std::snprintf(text,
                  sizeof(text),
                  "%u.%02u MHz is outside the EU transmit bands",
                  static_cast<unsigned>(frequency_hz / 1000000U),
                  static_cast<unsigned>((frequency_hz % 1000000U) / 10000U));
    reason = text;
    return false;
}

const char* transmit_bands_text() {
    return "433.05-434.79 and 868.0-868.6 MHz";
}

} // namespace model
