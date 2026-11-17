/*
 * SPDX-License-Identifier: MIT
 */

#include "protocols/rolling_codes.h"

#include "protocols/key_decoder.h"

#include <initializer_list>

namespace model {
namespace {

constexpr const char* kRollingProtocols[] = {
    /* Decoded by this app. */
    "KeeLoq",
    "Star Line",
    "Nice FloR-S",
    "CAME Atomo",
    "Faac SLH",
    "KingGates Stylo4k",
    "Alutech AT-4N",
    "Scher-Khan",
    "Somfy Telis",
    "Somfy Keytis",
    /* Not decoded here, but a file from a Flipper Zero may carry these names. */
    "Security+ 1.0",
    "Security+ 2.0",
    "KIA Seed",
    "iDo 117/111",
    "Hay21",
    "Revers_RB2",
    "Roger",
};

uint64_t reverse_bits_64(uint64_t value) {
    uint64_t result = 0;
    for (int bit = 0; bit < 64; ++bit) {
        result = (result << 1) | ((value >> bit) & 1U);
    }
    return result;
}

} // namespace

bool is_rolling_protocol(const std::string& protocol) {
    for (const char* name : kRollingProtocols) {
        if (protocol == name) {
            return true;
        }
    }
    return false;
}

bool rolling_identity(const std::string& protocol, uint64_t key, RollingIdentity& identity) {
    if (protocol == "KeeLoq" || protocol == "Star Line") {
        /* Sent least significant bit first: the fixed half ends up on top once reversed. */
        const auto fixed = static_cast<uint32_t>(reverse_bits_64(key) >> 32);
        if (protocol == "KeeLoq") {
            identity.serial = fixed & 0x0FFFFFFFU;
            identity.button = fixed >> 28;
        }
        else {
            identity.serial = fixed & 0x00FFFFFFU;
            identity.button = fixed >> 24;
        }
        return true;
    }
    if (protocol == "Somfy Telis" || protocol == "Somfy Keytis") {
        const uint64_t plain = key ^ (key >> 8);
        identity.serial = static_cast<uint32_t>(plain & 0xFFFFFFU);
        identity.button = static_cast<uint32_t>((plain >> 44) & 0xFU);
        return true;
    }
    return false;
}

bool find_rolling_code(const std::vector<int32_t>& pulses, std::string& protocol) {
    KeyDecoder decoder;
    DecodedKey key;
    const auto found = [&](int32_t duration) {
        if (decoder.feed(duration, key) && is_rolling_protocol(key.protocol)) {
            protocol = key.protocol;
            return true;
        }
        return false;
    };

    for (const int32_t duration : pulses) {
        if (found(duration)) {
            return true;
        }
    }
    /* Close a frame that runs to the end of the recording: a short pulse, then silence. */
    const bool ends_high = !pulses.empty() && pulses.back() > 0;
    for (const int32_t duration : {ends_high ? -100000 : 2000, ends_high ? 2000 : -100000, 2000, -100000}) {
        if (found(duration)) {
            return true;
        }
    }
    return false;
}

} // namespace model
