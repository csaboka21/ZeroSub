/*
 * SPDX-License-Identifier: MIT
 */

#include "rfid/em4100.h"

#include <cstdio>

namespace model {

uint32_t em4100_cycles_per_bit(const std::string& type) {
    if (type == "EM4100" || type == "EM4100/64") {
        return 64;
    }
    if (type == "EM4100/32") {
        return 32;
    }
    if (type == "EM4100/16") {
        return 16;
    }
    return 0;
}

bool rfid_can_emulate(const RfidKey& key) {
    return em4100_cycles_per_bit(key.type) != 0 && key.data.size() == kEm4100DataSize;
}

std::array<bool, kEm4100FrameBits> em4100_frame(const std::array<uint8_t, kEm4100DataSize>& id) {
    std::array<bool, kEm4100FrameBits> frame{};
    std::size_t position = 0;
    for (int index = 0; index < 9; ++index) {
        frame[position++] = true;
    }

    std::array<bool, 4> column_parity{};
    for (std::size_t row = 0; row < 10; ++row) {
        /* Row 0 is the high nibble of the first byte. */
        const uint8_t byte = id[row / 2];
        const uint8_t nibble = (row % 2 == 0) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0x0F);
        bool row_parity = false;
        for (int bit = 3; bit >= 0; --bit) {
            const bool value = ((nibble >> bit) & 1) != 0;
            frame[position++] = value;
            row_parity ^= value;
            column_parity[3 - bit] ^= value;
        }
        frame[position++] = row_parity;
    }
    for (bool parity : column_parity) {
        frame[position++] = parity;
    }
    frame[position] = false;
    return frame;
}

bool em4100_decode(const std::array<bool, kEm4100FrameBits>& frame, std::array<uint8_t, kEm4100DataSize>& id) {
    for (std::size_t index = 0; index < 9; ++index) {
        if (!frame[index]) {
            return false;
        }
    }
    if (frame[kEm4100FrameBits - 1]) {
        return false;
    }

    id.fill(0);
    std::array<bool, 4> column_parity{};
    std::size_t position = 9;
    for (std::size_t row = 0; row < 10; ++row) {
        uint8_t nibble = 0;
        bool row_parity = false;
        for (std::size_t bit = 0; bit < 4; ++bit) {
            const bool value = frame[position++];
            nibble = static_cast<uint8_t>((nibble << 1) | (value ? 1 : 0));
            row_parity ^= value;
            column_parity[bit] ^= value;
        }
        if (frame[position++] != row_parity) {
            return false;
        }
        id[row / 2] |= (row % 2 == 0) ? static_cast<uint8_t>(nibble << 4) : nibble;
    }
    for (std::size_t bit = 0; bit < 4; ++bit) {
        if (frame[position++] != column_parity[bit]) {
            return false;
        }
    }
    return true;
}

std::vector<bool> em4100_half_bits(const std::array<uint8_t, kEm4100DataSize>& id, bool inverted) {
    const auto frame = em4100_frame(id);
    std::vector<bool> halves;
    halves.reserve(frame.size() * 2);
    for (bool bit : frame) {
        const bool first = bit != inverted;
        halves.push_back(first);
        halves.push_back(!first);
    }
    return halves;
}

namespace {

/* Keeps the half-bit buffer to a bit over two frames. */
constexpr std::size_t kMaxHalves = kEm4100FrameBits * 2 * 2 + 4;
/* Decoding is attempted every time this many new half-bits have arrived. */
constexpr std::size_t kHalvesPerAttempt = 16;

} // namespace

Em4100Decoder::Em4100Decoder(uint32_t half_bit_us) : half_bit_us_(half_bit_us) {}

void Em4100Decoder::reset() {
    halves_.clear();
    new_halves_ = 0;
    have_id_ = false;
}

bool Em4100Decoder::take(std::array<uint8_t, kEm4100DataSize>& id) {
    if (!have_id_) {
        return false;
    }
    id = id_;
    have_id_ = false;
    return true;
}

void Em4100Decoder::feed(int32_t duration_us) {
    if (duration_us == 0) {
        return;
    }
    const bool level = duration_us > 0;
    const uint32_t length = static_cast<uint32_t>(level ? duration_us : -static_cast<int64_t>(duration_us));
    /* One half-bit is [0.5, 1.5) T long, two are [1.5, 2.5) T. Anything else is not this signal. */
    const uint32_t doubled = length * 2;
    std::size_t count = 0;
    if (doubled >= half_bit_us_ && doubled < half_bit_us_ * 3) {
        count = 1;
    }
    else if (doubled >= half_bit_us_ * 3 && doubled < half_bit_us_ * 5) {
        count = 2;
    }
    if (count == 0) {
        halves_.clear();
        return;
    }

    for (std::size_t index = 0; index < count; ++index) {
        halves_.push_back(level);
    }
    if (halves_.size() > kMaxHalves) {
        halves_.erase(halves_.begin(), halves_.begin() + static_cast<std::ptrdiff_t>(halves_.size() - kMaxHalves));
    }
    new_halves_ += count;
    if (halves_.size() >= kEm4100FrameBits * 2 && new_halves_ >= kHalvesPerAttempt) {
        new_halves_ = 0;
        try_decode();
    }
}

void Em4100Decoder::try_decode() {
    for (std::size_t phase = 0; phase < 2; ++phase) {
        /* Bits from half-bit pairs; a pair of equal halves is not Manchester and breaks the run. */
        std::vector<int8_t> bits;
        for (std::size_t index = phase; index + 1 < halves_.size(); index += 2) {
            bits.push_back(halves_[index] == halves_[index + 1] ? int8_t{-1} : static_cast<int8_t>(halves_[index] ? 1 : 0));
        }
        for (int inverted = 0; inverted < 2; ++inverted) {
            for (std::size_t start = 0; start + kEm4100FrameBits <= bits.size(); ++start) {
                std::array<bool, kEm4100FrameBits> frame{};
                bool valid = true;
                for (std::size_t bit = 0; bit < kEm4100FrameBits; ++bit) {
                    const int8_t value = bits[start + bit];
                    if (value < 0) {
                        valid = false;
                        break;
                    }
                    frame[bit] = (value == 1) != (inverted == 1);
                }
                std::array<uint8_t, kEm4100DataSize> id{};
                if (valid && em4100_decode(frame, id)) {
                    id_ = id;
                    have_id_ = true;
                    /* Start afresh, so the same frame is not reported twice. */
                    halves_.clear();
                    return;
                }
            }
        }
    }
}

std::string rfid_hex(const std::vector<uint8_t>& data) {
    std::string text;
    char byte[4];
    for (std::size_t index = 0; index < data.size(); ++index) {
        std::snprintf(byte, sizeof(byte), index == 0 ? "%02X" : " %02X", static_cast<unsigned>(data[index]));
        text += byte;
    }
    return text;
}

std::string em4100_card_number(const std::vector<uint8_t>& data) {
    if (data.size() != kEm4100DataSize) {
        return std::string();
    }
    const uint32_t number = (static_cast<uint32_t>(data[1]) << 24) | (static_cast<uint32_t>(data[2]) << 16) |
                            (static_cast<uint32_t>(data[3]) << 8) | data[4];
    char text[16];
    std::snprintf(text, sizeof(text), "%010lu", static_cast<unsigned long>(number));
    return text;
}

std::string em4100_facility_card(const std::vector<uint8_t>& data) {
    if (data.size() != kEm4100DataSize) {
        return std::string();
    }
    const unsigned card = (static_cast<unsigned>(data[3]) << 8) | data[4];
    char text[16];
    std::snprintf(text, sizeof(text), "%03u,%05u", static_cast<unsigned>(data[2]), card);
    return text;
}

} // namespace model
