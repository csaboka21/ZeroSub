/*
 * SPDX-License-Identifier: MIT
 *
 * Infrared remote-control protocols. Timings, bit layouts and names follow the
 * Flipper Zero firmware (lib/infrared/encoder_decoder), so a message decoded
 * here is written to an .ir file exactly as a Flipper would write it.
 *
 * Decoding works on one whole frame at a time and matches on the length of a
 * mark plus the space after it wherever it can. An infrared receiver module
 * stretches every mark and shortens every space by the same amount, often by
 * more than 100 us, but the sum of the two stays what the remote sent.
 */

#include "ir_protocols.h"

#include <array>
#include <cstring>
#include <utility>

namespace model {
namespace {

/* ---- Protocol table ------------------------------------------------------ */

struct Variant {
    const char* name;
    uint32_t address_bits;
    uint32_t command_bits;
    uint32_t carrier_hz;
};

constexpr Variant kVariants[] = {
    {"NEC", 8, 8, 38000},       {"NECext", 16, 16, 38000}, {"NEC42", 13, 8, 38000},
    {"NEC42ext", 26, 16, 38000}, {"Samsung32", 8, 8, 38000}, {"RC5", 5, 6, 36000},
    {"RC5X", 5, 6, 36000},      {"RC6", 8, 8, 36000},      {"SIRC", 5, 7, 40000},
    {"SIRC15", 8, 7, 40000},    {"SIRC20", 13, 7, 40000},  {"Kaseikyo", 26, 10, 38000},
    {"RCA", 4, 8, 38000},       {"Pioneer", 8, 8, 40000},
};

const Variant* find_variant(const std::string& name) {
    for (const auto& variant : kVariants) {
        if (name == variant.name) {
            return &variant;
        }
    }
    return nullptr;
}

/* The table's own string for a name, so messages can hold a plain pointer. */
const char* static_name(const char* name) {
    const Variant* variant = find_variant(name);
    return variant ? variant->name : nullptr;
}

uint32_t mask(uint32_t bits) {
    return bits >= 32 ? 0xFFFFFFFFU : ((1U << bits) - 1U);
}

uint32_t distance(uint32_t left, uint32_t right) {
    return left > right ? left - right : right - left;
}

bool near(uint32_t value, uint32_t nominal, uint32_t percent) {
    return distance(value, nominal) * 100U <= nominal * percent;
}

/* ---- Pulse-distance protocols: NEC, Samsung, Kaseikyo, RCA, Pioneer ------- */

enum class Family {
    Nec,
    Samsung,
    Kaseikyo,
    Rca,
    Pioneer,
};

struct DistanceSpec {
    Family family;
    uint32_t header_mark;
    uint32_t header_space;
    uint32_t bit_mark;
    uint32_t one_space;
    uint32_t zero_space;
    /* Frame lengths in bits; 0 for an unused slot. */
    std::array<uint32_t, 2> bits;
};

constexpr DistanceSpec kDistanceSpecs[] = {
    {Family::Nec, 9000, 4500, 560, 1690, 560, {32, 42}},
    {Family::Samsung, 4500, 4500, 550, 1650, 550, {32, 0}},
    {Family::Kaseikyo, 3456, 1728, 432, 1296, 432, {48, 0}},
    {Family::Rca, 4000, 4000, 500, 2000, 1000, {24, 0}},
    /* Pioneer sends 32 data bits and then one more, always 0. */
    {Family::Pioneer, 8500, 4225, 500, 1500, 500, {33, 32}},
};

const DistanceSpec& spec_of(Family family) {
    for (const auto& spec : kDistanceSpecs) {
        if (spec.family == family) {
            return spec;
        }
    }
    return kDistanceSpecs[0];
}

/* Splits the data bits of a frame into a message, checking whatever the protocol lets us check. */
bool interpret_distance(Family family, uint64_t data, uint32_t bits, IrMessage& message) {
    const auto byte = [data](unsigned index) { return static_cast<uint8_t>(data >> (8 * index)); };

    switch (family) {
        case Family::Nec:
            if (bits == 32) {
                if (byte(0) == static_cast<uint8_t>(~byte(1)) && byte(2) == static_cast<uint8_t>(~byte(3))) {
                    message = {static_name("NEC"), byte(0), byte(2)};
                }
                else {
                    message = {static_name("NECext"),
                               static_cast<uint32_t>(data & 0xFFFF),
                               static_cast<uint32_t>((data >> 16) & 0xFFFF)};
                }
                return true;
            }
            if (bits == 42) {
                const auto address = static_cast<uint32_t>(data & 0x1FFF);
                const auto address_inverse = static_cast<uint32_t>((data >> 13) & 0x1FFF);
                const auto command = static_cast<uint32_t>((data >> 26) & 0xFF);
                const auto command_inverse = static_cast<uint32_t>((data >> 34) & 0xFF);
                if (address == (~address_inverse & 0x1FFF) && command == (~command_inverse & 0xFF)) {
                    message = {static_name("NEC42"), address, command};
                }
                else {
                    message = {static_name("NEC42ext"),
                               address | (address_inverse << 13),
                               command | (command_inverse << 8)};
                }
                return true;
            }
            return false;

        case Family::Samsung:
            if (byte(0) != byte(1) || byte(2) != static_cast<uint8_t>(~byte(3))) {
                return false;
            }
            message = {static_name("Samsung32"), byte(0), byte(2)};
            return true;

        case Family::Kaseikyo: {
            const uint32_t vendor = byte(0) | (static_cast<uint32_t>(byte(1)) << 8);
            const uint8_t vendor_parity = byte(2) & 0x0F;
            const uint8_t genre1 = byte(2) >> 4;
            const uint8_t genre2 = byte(3) & 0x0F;
            const uint32_t command = (byte(3) >> 4) | (static_cast<uint32_t>(byte(4) & 0x3F) << 4);
            const uint8_t id = byte(4) >> 6;
            uint8_t vendor_check = byte(0) ^ byte(1);
            vendor_check = (vendor_check & 0x0F) ^ (vendor_check >> 4);
            if (vendor_parity != vendor_check || byte(5) != (byte(2) ^ byte(3) ^ byte(4))) {
                return false;
            }
            message = {static_name("Kaseikyo"),
                       (static_cast<uint32_t>(id) << 24) | (vendor << 8) |
                           (static_cast<uint32_t>(genre1) << 4) | genre2,
                       command};
            return true;
        }

        case Family::Rca: {
            const auto address = static_cast<uint32_t>(data & 0xF);
            const auto command = static_cast<uint32_t>((data >> 4) & 0xFF);
            const auto address_inverse = static_cast<uint32_t>((data >> 12) & 0xF);
            const auto command_inverse = static_cast<uint32_t>((data >> 16) & 0xFF);
            if (address != (~address_inverse & 0xF) || command != (~command_inverse & 0xFF)) {
                return false;
            }
            message = {static_name("RCA"), address, command};
            return true;
        }

        case Family::Pioneer:
            if (byte(0) != static_cast<uint8_t>(~byte(1)) || byte(2) != static_cast<uint8_t>(~byte(3))) {
                return false;
            }
            message = {static_name("Pioneer"), byte(0), byte(2)};
            return true;
    }
    return false;
}

/*
 * Tries one pulse-distance protocol on a frame. On a match `error_permille`
 * says how far the frame's timing is from the protocol's, so that the closest
 * of two similar protocols (NEC and Pioneer) can be picked.
 */
bool match_distance(const DistanceSpec& spec,
                    const std::vector<uint32_t>& frame,
                    IrMessage& message,
                    uint32_t& error_permille) {
    /* Header mark and space, a mark and a space per bit, and the closing mark. */
    if (frame.size() < 5 || frame.size() % 2 == 0) {
        return false;
    }
    const auto bits = static_cast<uint32_t>((frame.size() - 3) / 2);
    if (bits == 0 || (bits != spec.bits[0] && bits != spec.bits[1])) {
        return false;
    }
    if (!near(frame[0], spec.header_mark, 25) || !near(frame[1], spec.header_space, 25)) {
        return false;
    }

    const uint32_t one_period = spec.bit_mark + spec.one_space;
    const uint32_t zero_period = spec.bit_mark + spec.zero_space;
    const uint32_t header_period = spec.header_mark + spec.header_space;
    uint64_t error = distance(frame[0] + frame[1], header_period);
    uint64_t nominal = header_period;
    uint64_t data = 0;

    for (uint32_t bit = 0; bit < bits; ++bit) {
        const uint32_t mark = frame[2 + 2 * bit];
        const uint32_t period = mark + frame[3 + 2 * bit];
        if (mark < spec.bit_mark / 3 || mark > spec.bit_mark * 2 + 100) {
            return false;
        }
        const bool one = distance(period, one_period) < distance(period, zero_period);
        const uint32_t expected = one ? one_period : zero_period;
        if (!near(period, expected, 20)) {
            return false;
        }
        error += distance(period, expected);
        nominal += expected;
        if (one) {
            data |= 1ULL << bit;
        }
    }
    const uint32_t stop = frame.back();
    if (stop < spec.bit_mark / 3 || stop > spec.bit_mark * 2 + 100) {
        return false;
    }

    if (!interpret_distance(spec.family, data, bits, message)) {
        return false;
    }
    error_permille = static_cast<uint32_t>(error * 1000U / nominal);
    return true;
}

/* The short frames NEC and Samsung remotes send while a button is held. */
bool is_repeat_frame(const std::vector<uint32_t>& frame) {
    if (frame.size() == 3) {
        return near(frame[0], 9000, 25) && near(frame[1], 2250, 25) && frame[2] > 180 && frame[2] < 1200;
    }
    if (frame.size() == 5) {
        return near(frame[0], 4500, 25) && near(frame[1], 4500, 25) && near(frame[2] + frame[3], 2200, 20) &&
               frame[4] > 180 && frame[4] < 1200;
    }
    return false;
}

/* ---- SIRC (Sony): the mark carries the bit -------------------------------- */

constexpr uint32_t kSircHeaderMark = 2400;
constexpr uint32_t kSircUnit = 600;
constexpr uint32_t kSircRepeatPeriod = 45000;
constexpr int kSircFrames = 3;

bool match_sirc(const std::vector<uint32_t>& frame, IrMessage& message) {
    /* Header mark and space, then a mark per bit with spaces between them only. */
    if (frame.size() < 5 || frame.size() % 2 == 0) {
        return false;
    }
    const auto bits = static_cast<uint32_t>((frame.size() - 1) / 2);
    if (bits != 12 && bits != 15 && bits != 20) {
        return false;
    }
    /* The header as a whole tells SIRC (3000 us) from RC6 (3555 us), which is otherwise similar. */
    if (!near(frame[0], kSircHeaderMark, 25) || !near(frame[0] + frame[1], kSircHeaderMark + kSircUnit, 8)) {
        return false;
    }

    uint32_t data = 0;
    for (uint32_t bit = 0; bit < bits; ++bit) {
        const uint32_t mark = frame[2 + 2 * bit];
        if (mark < 300 || mark > 1700) {
            return false;
        }
        bool one = false;
        if (bit + 1 < bits) {
            const uint32_t space = frame[3 + 2 * bit];
            if (space < 250 || space > 1100) {
                return false;
            }
            const uint32_t period = mark + space;
            one = period > 3 * kSircUnit - kSircUnit / 2;
            if (!near(period, one ? 3 * kSircUnit : 2 * kSircUnit, 15)) {
                return false;
            }
        }
        else {
            /* The last bit has no space after it. */
            one = mark > kSircUnit + kSircUnit / 2;
        }
        if (one) {
            data |= 1U << bit;
        }
    }

    const uint32_t command = data & 0x7F;
    if (bits == 12) {
        message = {static_name("SIRC"), (data >> 7) & 0x1F, command};
    }
    else if (bits == 15) {
        message = {static_name("SIRC15"), (data >> 7) & 0xFF, command};
    }
    else {
        message = {static_name("SIRC20"), (data >> 7) & 0x1FFF, command};
    }
    return true;
}

/* ---- RC5 and RC6 (Philips): Manchester coded ------------------------------ */

constexpr uint32_t kRc5Unit = 889;
constexpr uint32_t kRc6Unit = 444;
constexpr uint32_t kRc6LeaderMark = 2666;
constexpr uint32_t kRc6LeaderSpace = 889;
constexpr std::size_t kRc5Halves = 28;
constexpr std::size_t kRc6Halves = 44;

/*
 * Expands durations (mark first) into one level per time unit. Each mark is
 * sized together with the space after it, since that sum is not distorted by
 * the receiver; the mark alone then only has to be roughly right.
 */
bool to_levels(const std::vector<uint32_t>& frame,
               std::size_t first,
               uint32_t unit,
               std::size_t max_levels,
               std::vector<bool>& levels) {
    const auto units_of = [unit](uint32_t duration) { return (duration + unit / 2) / unit; };

    for (std::size_t index = first; index < frame.size(); index += 2) {
        const uint32_t mark = frame[index];
        uint32_t mark_units = units_of(mark);
        uint32_t space_units = 0;
        if (index + 1 < frame.size()) {
            const uint32_t pair = mark + frame[index + 1];
            const uint32_t pair_units = units_of(pair);
            if (pair_units < 2 || distance(pair, pair_units * unit) * 10U > unit * 4U) {
                return false;
            }
            if (mark_units < 1) {
                mark_units = 1;
            }
            if (mark_units > pair_units - 1) {
                mark_units = pair_units - 1;
            }
            space_units = pair_units - mark_units;
        }
        if (mark_units < 1 || mark_units > 3 || space_units > 3 ||
            distance(mark, mark_units * unit) * 10U > unit * 6U) {
            return false;
        }
        levels.insert(levels.end(), mark_units, true);
        levels.insert(levels.end(), space_units, false);
        if (levels.size() > max_levels) {
            return false;
        }
    }
    return true;
}

bool match_rc5(const std::vector<uint32_t>& frame, IrMessage& message) {
    if (frame.size() < 11 || frame.size() > kRc5Halves) {
        return false;
    }
    /* The first start bit is space-then-mark, and that space cannot be seen. */
    std::vector<bool> levels{false};
    if (!to_levels(frame, 0, kRc5Unit, kRc5Halves, levels) || levels.size() < kRc5Halves - 1) {
        return false;
    }
    levels.resize(kRc5Halves, false);

    uint32_t data = 0;
    for (std::size_t bit = 0; bit < kRc5Halves / 2; ++bit) {
        const bool first = levels[2 * bit];
        const bool second = levels[2 * bit + 1];
        if (first == second) {
            return false;
        }
        /* Space then mark is a 1. Bits arrive most significant first. */
        data = (data << 1) | (second ? 1U : 0U);
    }
    /* 14 bits: start, field, toggle, 5 address, 6 command. */
    if ((data & 0x2000) == 0) {
        return false;
    }
    const bool field = (data & 0x1000) != 0;
    message = {static_name(field ? "RC5" : "RC5X"), (data >> 6) & 0x1F, data & 0x3F};
    return true;
}

bool match_rc6(const std::vector<uint32_t>& frame, IrMessage& message) {
    if (frame.size() < 12 || !near(frame[0], kRc6LeaderMark, 25) ||
        !near(frame[0] + frame[1], kRc6LeaderMark + kRc6LeaderSpace, 8)) {
        return false;
    }
    std::vector<bool> levels;
    if (!to_levels(frame, 2, kRc6Unit, kRc6Halves, levels) || levels.size() < kRc6Halves - 1) {
        return false;
    }
    levels.resize(kRc6Halves, false);

    /* Mark then space is a 1. Units 0-7: start bit and three mode bits. */
    const auto bit_at = [&levels](std::size_t unit, bool& value) {
        if (levels[unit] == levels[unit + 1]) {
            return false;
        }
        value = levels[unit];
        return true;
    };
    bool value = false;
    if (!bit_at(0, value) || !value) {
        return false;
    }
    for (std::size_t unit = 2; unit < 8; unit += 2) {
        /* Only mode 0 is the plain RC6 remote-control frame. */
        if (!bit_at(unit, value) || value) {
            return false;
        }
    }
    /* Units 8-11: the toggle bit, twice as long as the others. */
    if (levels[8] != levels[9] || levels[10] != levels[11] || levels[8] == levels[10]) {
        return false;
    }
    uint32_t data = 0;
    for (std::size_t unit = 12; unit < kRc6Halves; unit += 2) {
        if (!bit_at(unit, value)) {
            return false;
        }
        data = (data << 1) | (value ? 1U : 0U);
    }
    message = {static_name("RC6"), (data >> 8) & 0xFF, data & 0xFF};
    return true;
}

/* ---- Encoding -------------------------------------------------------------- */

void append_distance_frame(const DistanceSpec& spec, uint64_t data, uint32_t bits, std::vector<uint32_t>& timings) {
    timings.push_back(spec.header_mark);
    timings.push_back(spec.header_space);
    for (uint32_t bit = 0; bit < bits; ++bit) {
        timings.push_back(spec.bit_mark);
        timings.push_back(((data >> bit) & 1U) != 0 ? spec.one_space : spec.zero_space);
    }
    timings.push_back(spec.bit_mark);
}

uint64_t total_of(const std::vector<uint32_t>& timings, std::size_t first) {
    uint64_t total = 0;
    for (std::size_t index = first; index < timings.size(); ++index) {
        total += timings[index];
    }
    return total;
}

/* Turns one level per time unit into durations, dropping silence at either end. */
void append_levels(const std::vector<std::pair<bool, uint32_t>>& levels, std::vector<uint32_t>& timings) {
    bool started = false;
    bool current = false;
    for (const auto& level : levels) {
        if (!started) {
            if (!level.first) {
                continue;
            }
            started = true;
            current = true;
            timings.push_back(level.second);
        }
        else if (level.first == current) {
            timings.back() += level.second;
        }
        else {
            current = level.first;
            timings.push_back(level.second);
        }
    }
    if (started && !current) {
        timings.pop_back();
    }
}

/* Philips remotes flip this bit on every new press, so a receiver can tell a press from a hold. */
bool next_toggle() {
    static bool toggle = false;
    toggle = !toggle;
    return toggle;
}

} // namespace

bool IrMessage::same_as(const IrMessage& other) const {
    return protocol && other.protocol && std::strcmp(protocol, other.protocol) == 0 &&
           address == other.address && command == other.command;
}

IrFrameKind ir_decode_frame(const std::vector<uint32_t>& frame, IrMessage& message) {
    if (is_repeat_frame(frame)) {
        return IrFrameKind::Repeat;
    }

    bool found = false;
    uint32_t best_error = 0;
    for (const auto& spec : kDistanceSpecs) {
        IrMessage candidate;
        uint32_t error = 0;
        if (match_distance(spec, frame, candidate, error) && (!found || error < best_error)) {
            found = true;
            best_error = error;
            message = candidate;
        }
    }
    if (found || match_sirc(frame, message) || match_rc6(frame, message) || match_rc5(frame, message)) {
        return IrFrameKind::Message;
    }
    return IrFrameKind::Unknown;
}

bool ir_protocol_widths(const std::string& protocol, uint32_t& address_bits, uint32_t& command_bits) {
    const Variant* variant = find_variant(protocol);
    if (!variant) {
        return false;
    }
    address_bits = variant->address_bits;
    command_bits = variant->command_bits;
    return true;
}

const char* ir_protocol_name(const std::string& protocol) {
    const Variant* variant = find_variant(protocol);
    return variant ? variant->name : nullptr;
}

bool ir_encode(const std::string& protocol,
               uint32_t address,
               uint32_t command,
               std::vector<uint32_t>& timings,
               IrCarrier& carrier,
               std::string& error) {
    error.clear();
    timings.clear();

    const Variant* variant = find_variant(protocol);
    if (!variant) {
        error = "Unknown protocol: " + protocol;
        return false;
    }
    carrier.frequency_hz = variant->carrier_hz;
    carrier.duty_cycle = 0.33F;
    address &= mask(variant->address_bits);
    command &= mask(variant->command_bits);
    const std::string name = variant->name;

    if (name == "NEC") {
        const uint64_t data = address | ((~address & 0xFFU) << 8) | (command << 16) |
                              (static_cast<uint64_t>(~command & 0xFFU) << 24);
        append_distance_frame(spec_of(Family::Nec), data, 32, timings);
    }
    else if (name == "NECext") {
        append_distance_frame(spec_of(Family::Nec), address | (static_cast<uint64_t>(command) << 16), 32, timings);
    }
    else if (name == "NEC42") {
        const uint64_t data = address | (static_cast<uint64_t>(~address & 0x1FFFU) << 13) |
                              (static_cast<uint64_t>(command) << 26) |
                              (static_cast<uint64_t>(~command & 0xFFU) << 34);
        append_distance_frame(spec_of(Family::Nec), data, 42, timings);
    }
    else if (name == "NEC42ext") {
        append_distance_frame(spec_of(Family::Nec), address | (static_cast<uint64_t>(command) << 26), 42, timings);
    }
    else if (name == "Samsung32") {
        const uint64_t data = address | (address << 8) | (command << 16) |
                              (static_cast<uint64_t>(~command & 0xFFU) << 24);
        append_distance_frame(spec_of(Family::Samsung), data, 32, timings);
    }
    else if (name == "Kaseikyo") {
        const uint32_t id = (address >> 24) & 0x3;
        const uint32_t vendor = (address >> 8) & 0xFFFF;
        const uint32_t genre1 = (address >> 4) & 0xF;
        const uint32_t genre2 = address & 0xF;
        std::array<uint8_t, 6> bytes{};
        bytes[0] = static_cast<uint8_t>(vendor & 0xFF);
        bytes[1] = static_cast<uint8_t>(vendor >> 8);
        uint8_t vendor_parity = bytes[0] ^ bytes[1];
        vendor_parity = (vendor_parity & 0x0F) ^ (vendor_parity >> 4);
        bytes[2] = static_cast<uint8_t>((vendor_parity & 0x0F) | (genre1 << 4));
        bytes[3] = static_cast<uint8_t>(genre2 | ((command & 0xF) << 4));
        bytes[4] = static_cast<uint8_t>((id << 6) | (command >> 4));
        bytes[5] = bytes[2] ^ bytes[3] ^ bytes[4];
        uint64_t data = 0;
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            data |= static_cast<uint64_t>(bytes[index]) << (8 * index);
        }
        append_distance_frame(spec_of(Family::Kaseikyo), data, 48, timings);
    }
    else if (name == "RCA") {
        const uint64_t data = address | (command << 4) | ((~address & 0xFU) << 12) |
                              ((~command & 0xFFU) << 16);
        append_distance_frame(spec_of(Family::Rca), data, 24, timings);
    }
    else if (name == "Pioneer") {
        const uint64_t data = address | ((~address & 0xFFU) << 8) | (command << 16) |
                              (static_cast<uint64_t>(~command & 0xFFU) << 24);
        /* The frame is sent twice, 26 ms apart; receivers ignore a single one. */
        append_distance_frame(spec_of(Family::Pioneer), data, 33, timings);
        timings.push_back(26000);
        append_distance_frame(spec_of(Family::Pioneer), data, 33, timings);
    }
    else if (name == "SIRC" || name == "SIRC15" || name == "SIRC20") {
        const uint32_t bits = variant->address_bits + variant->command_bits;
        const uint32_t data = command | (address << 7);
        for (int frame = 0; frame < kSircFrames; ++frame) {
            const std::size_t start = timings.size();
            timings.push_back(kSircHeaderMark);
            timings.push_back(kSircUnit);
            for (uint32_t bit = 0; bit < bits; ++bit) {
                timings.push_back(((data >> bit) & 1U) != 0 ? 2 * kSircUnit : kSircUnit);
                if (bit + 1 < bits) {
                    timings.push_back(kSircUnit);
                }
            }
            if (frame + 1 < kSircFrames) {
                /* Frames start 45 ms apart, whatever their length. */
                timings.push_back(kSircRepeatPeriod - static_cast<uint32_t>(total_of(timings, start)));
            }
        }
    }
    else if (name == "RC5" || name == "RC5X") {
        /* 14 bits, most significant first: start, field, toggle, address, command. */
        const uint32_t data = 0x2000U | (name == "RC5" ? 0x1000U : 0U) | (next_toggle() ? 0x800U : 0U) |
                              (address << 6) | command;
        std::vector<std::pair<bool, uint32_t>> levels;
        for (int bit = 13; bit >= 0; --bit) {
            const bool one = ((data >> bit) & 1U) != 0;
            levels.emplace_back(!one, kRc5Unit);
            levels.emplace_back(one, kRc5Unit);
        }
        append_levels(levels, timings);
    }
    else if (name == "RC6") {
        std::vector<std::pair<bool, uint32_t>> levels;
        levels.emplace_back(true, kRc6LeaderMark);
        levels.emplace_back(false, kRc6LeaderSpace);
        const auto add_bit = [&levels](bool one, uint32_t unit) {
            levels.emplace_back(one, unit);
            levels.emplace_back(!one, unit);
        };
        add_bit(true, kRc6Unit);
        for (int mode = 0; mode < 3; ++mode) {
            add_bit(false, kRc6Unit);
        }
        add_bit(next_toggle(), 2 * kRc6Unit);
        const uint32_t data = (address << 8) | command;
        for (int bit = 15; bit >= 0; --bit) {
            add_bit(((data >> bit) & 1U) != 0, kRc6Unit);
        }
        append_levels(levels, timings);
    }

    if (timings.empty()) {
        error = "Cannot encode " + protocol;
        return false;
    }
    return true;
}

/* ---- Collector ------------------------------------------------------------- */

namespace {

/* Silence after which a frame that ended on a mark is taken to be complete. */
constexpr uint32_t kFrameIdleUs = 20000;

} // namespace

void IrCollector::reset() {
    timings_.clear();
    frame_start_ = 0;
    frame_open_ = false;
    decoded_ = false;
    overflowed_ = false;
    last_message_ = IrMessage{};
}

bool IrCollector::busy() const {
    return !timings_.empty();
}

void IrCollector::feed(bool mark, uint32_t duration_us, std::vector<IrCapture>& out) {
    if (duration_us == 0) {
        return;
    }
    const bool last_is_mark = timings_.size() % 2 == 1;

    if (mark) {
        if (overflowed_) {
            return;
        }
        if (last_is_mark) {
            timings_.back() += duration_us;
        }
        else if (timings_.size() >= kMaxTimings) {
            /* Keep what fits; the rest of the signal is dropped. */
            overflowed_ = true;
            return;
        }
        else {
            if (!frame_open_) {
                frame_start_ = timings_.size();
            }
            timings_.push_back(duration_us);
        }
        frame_open_ = true;
        return;
    }

    if (timings_.empty()) {
        /* Silence before the first mark is not part of anything. */
        return;
    }
    if (duration_us >= kSignalGapUs) {
        end_signal(out);
        return;
    }
    if (overflowed_) {
        return;
    }
    if (last_is_mark) {
        timings_.push_back(duration_us);
    }
    else {
        timings_.back() += duration_us;
    }
    if (timings_.back() >= kSignalGapUs) {
        end_signal(out);
    }
    else if (timings_.back() >= kFrameGapUs) {
        end_frame(out);
    }
}

void IrCollector::idle(uint32_t silent_us, std::vector<IrCapture>& out) {
    if (timings_.empty()) {
        return;
    }
    if (silent_us >= kSignalGapUs) {
        end_signal(out);
    }
    else if (silent_us >= kFrameIdleUs) {
        end_frame(out);
    }
}

void IrCollector::end_frame(std::vector<IrCapture>& out) {
    if (!frame_open_) {
        return;
    }
    frame_open_ = false;

    /* The frame runs from its first mark to its last one. */
    std::size_t end = timings_.size();
    if (end % 2 == 0) {
        --end;
    }
    if (end <= frame_start_) {
        return;
    }
    const std::vector<uint32_t> frame(timings_.begin() + static_cast<std::ptrdiff_t>(frame_start_),
                                      timings_.begin() + static_cast<std::ptrdiff_t>(end));

    IrMessage message;
    switch (ir_decode_frame(frame, message)) {
        case IrFrameKind::Message:
            decoded_ = true;
            last_message_ = message;
            break;
        case IrFrameKind::Repeat:
            /* Only means something after the frame it repeats. */
            if (!decoded_) {
                return;
            }
            break;
        case IrFrameKind::Unknown:
            return;
    }
    IrCapture capture;
    capture.message = last_message_;
    out.push_back(std::move(capture));
}

void IrCollector::end_signal(std::vector<IrCapture>& out) {
    end_frame(out);

    if (!decoded_) {
        if (timings_.size() % 2 == 0 && !timings_.empty()) {
            timings_.pop_back();
        }
        if (timings_.size() >= kMinRawTimings) {
            IrCapture capture;
            capture.raw = true;
            capture.timings = std::move(timings_);
            out.push_back(std::move(capture));
        }
    }
    reset();
}

} // namespace model
