/*
 * SPDX-License-Identifier: MIT
 */

#include "protocols/stream_decoders.h"

#include "protocols/key_encoder.h"

#include <algorithm>

namespace model {

/* ---- Shared pieces ------------------------------------------------------ */

namespace {

struct Window {
    uint32_t min;
    uint32_t max;

    bool set() const {
        return max > 0;
    }
    bool has(uint32_t value) const {
        return value >= min && value <= max;
    }
};

struct Pulse {
    uint32_t nominal;
    uint32_t tolerance;

    bool fits(uint32_t value) const {
        const uint32_t distance = value > nominal ? value - nominal : nominal - value;
        return distance < tolerance;
    }
};

constexpr Window kNone{0, 0};

bool near(uint32_t value, uint32_t nominal, uint32_t tolerance) {
    return Pulse{nominal, tolerance}.fits(value);
}

} // namespace

/* How a pulse-width frame ends. */
enum class PwmEnd {
    /* Low-first bits: a long low arrives where the next bit would start. */
    LowAfterBits,
    /* High-first bits: the last bit has no low of its own, the gap takes its place. */
    LowCutsLastBit,
    /* High-first bits: all bits are complete, then one more pulse, then the gap. */
    LowAfterTrailingPulse,
    /* High-first bits: a long high arrives where the next bit would start. */
    HighAfterBits,
};

struct PwmSpec {
    const char* name;
    uint32_t bits;
    /* Header: an optional silent gap, an optional start pulse, an optional low after it. */
    Window gap_low;
    Window start_high;
    Window start_low;
    /* A bit is (high, low) when set, otherwise (low, high). */
    bool high_first;
    Pulse zero_first;
    Pulse zero_second;
    Pulse one_first;
    Pulse one_second;
    /* The second half of a bit may be either length (BETT). */
    bool loose_second;
    PwmEnd end;
    uint32_t end_min;
    bool (*validate)(uint64_t data);
    /*
     * Frames longer than the key: `bits` is how many leading bits are kept, and
     * the frame must hold between total_min and total_max bits. Zero means
     * exactly `bits`. When total_min is above `bits`, the rest of the frame is
     * reported as the key's extra bits. report_bits is the length shown for the key; zero means
     * the number of bits received.
     */
    uint32_t total_min;
    uint32_t total_max;
    uint32_t report_bits;
};

namespace {

bool holtek_valid(uint64_t data) {
    return (data & 0xF000000000ULL) == 0x5000000000ULL;
}

bool hormann_valid(uint64_t data) {
    return (data & 0xFF000000003ULL) == 0xFF000000003ULL;
}

bool magellan_valid(uint64_t data) {
    /* The low byte is a CRC-8 (polynomial 0x31, start 0) of the three bytes above it. */
    uint8_t crc = 0;
    for (int shift = 24; shift >= 8; shift -= 8) {
        crc ^= static_cast<uint8_t>(data >> shift);
        for (int bit = 0; bit < 8; ++bit) {
            crc = static_cast<uint8_t>((crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1);
        }
    }
    return static_cast<uint8_t>(data) == crc;
}

bool honeywell_valid(uint64_t data) {
    /* The last bit is the parity of the 47 bits before it. */
    unsigned ones = 0;
    for (int bit = 1; bit < 48; ++bit) {
        ones += static_cast<unsigned>((data >> bit) & 1U);
    }
    return (data & 1U) == (ones & 1U);
}

/* clang-format off */
constexpr PwmSpec kPwmSpecs[] = {
    /* name, bits, gap, start high, start low, high first, zero, one, loose, end, end min, check */
    {"Holtek", 40, {11880, 19080}, {330, 530}, kNone, false,
     {430, 100}, {870, 200}, {870, 200}, {430, 100}, false, PwmEnd::LowAfterBits, 4400, holtek_valid, 0, 0, 0},
    {"GateTX", 24, {11750, 21150}, {400, 1000}, kNone, false,
     {350, 100}, {700, 300}, {700, 300}, {350, 100}, false, PwmEnd::LowAfterBits, 3600, nullptr, 0, 0, 0},
    {"SMC5326", 25, {4800, 12000}, kNone, kNone, true,
     {300, 200}, {900, 600}, {900, 600}, {300, 200}, false, PwmEnd::LowAfterTrailingPulse, 1800, nullptr, 0, 0, 0},
    {"LinearDelta3", 8, {31400, 38600}, kNone, kNone, true,
     {2000, 300}, {2000, 300}, {500, 150}, {3500, 450}, false, PwmEnd::LowCutsLastBit, 5000, nullptr, 0, 0, 0},
    {"Clemsa", 18, {15885, 23385}, kNone, kNone, true,
     {385, 150}, {2695, 450}, {2695, 450}, {385, 150}, false, PwmEnd::LowCutsLastBit, 10000, nullptr, 0, 0, 0},
    {"BETT", 18, {12710, 17210}, kNone, kNone, true,
     {340, 150}, {2000, 450}, {2000, 450}, {340, 150}, true, PwmEnd::LowCutsLastBit, 10000, nullptr, 0, 0, 0},
    {"Doitrand", 37, {20300, 29300}, {350, 1250}, kNone, false,
     {400, 150}, {1100, 450}, {1100, 450}, {400, 150}, false, PwmEnd::LowAfterBits, 4150, nullptr, 0, 0, 0},
    {"Dooya", 40, {6396, 11196}, {4158, 5358}, {1106, 1826}, true,
     {366, 120}, {733, 240}, {733, 240}, {366, 120}, false, PwmEnd::LowCutsLastBit, 2932, nullptr, 0, 0, 0},
    {"Phoenix_V2", 52, {22620, 28620}, {2162, 2962}, kNone, false,
     {853, 300}, {427, 100}, {427, 100}, {853, 300}, false, PwmEnd::LowAfterBits, 4370, nullptr, 0, 0, 0},
    {"Hormann HSM", 44, kNone, {7200, 16800}, {300, 700}, true,
     {500, 200}, {1000, 200}, {1000, 200}, {500, 200}, false, PwmEnd::HighAfterBits, 2500, hormann_valid, 0, 0, 0},
    {"Nero Sketch", 40, kNone, {1170, 1470}, {180, 480}, true,
     {330, 150}, {660, 150}, {660, 150}, {330, 150}, false, PwmEnd::HighAfterBits, 850, nullptr, 0, 0, 0},
    {"Nero Radio", 56, kNone, {720, 880}, {120, 280}, true,
     {200, 80}, {400, 80}, {400, 80}, {200, 80}, false, PwmEnd::LowCutsLastBit, 2160, nullptr, 0, 0, 0},
    {"Magellan", 32, kNone, {900, 1500}, {200, 600}, true,
     {400, 100}, {200, 100}, {200, 100}, {400, 100}, false, PwmEnd::LowAfterTrailingPulse, 1200, magellan_valid, 0, 0, 0},
    {"Honeywell", 48, {420, 540}, kNone, kNone, true,
     {160, 60}, {320, 60}, {320, 60}, {160, 60}, false, PwmEnd::HighAfterBits, 420, honeywell_valid, 0, 0, 0},

    /*
     * Rolling codes. These are recognised so they can be shown and kept from
     * being sent; nothing here can produce the next code.
     */
    {"KeeLoq", 64, kNone, {260, 540}, {2600, 5400}, true,
     {800, 280}, {400, 140}, {400, 140}, {800, 280}, false, PwmEnd::LowAfterTrailingPulse, 940, nullptr, 64, 66, 64},
    {"Alutech AT-4N", 64, kNone, {260, 540}, {2600, 5400}, true,
     {800, 280}, {400, 140}, {400, 140}, {800, 280}, false, PwmEnd::LowCutsLastBit, 940, nullptr, 72, 72, 72},
    {"Star Line", 64, kNone, {760, 1240}, {760, 1240}, true,
     {250, 120}, {250, 120}, {500, 120}, {500, 120}, false, PwmEnd::HighAfterBits, 620, nullptr, 0, 0, 0},
    {"Nice FloR-S", 52, {7600, 30400}, {600, 2400}, {600, 2400}, true,
     {500, 300}, {1000, 300}, {1000, 300}, {500, 300}, false, PwmEnd::HighAfterBits, 1200, nullptr, 0, 0, 0},
    {"Nice FloR-S", 52, {7600, 30400}, {600, 2400}, {600, 2400}, true,
     {500, 300}, {1000, 300}, {1000, 300}, {500, 300}, false, PwmEnd::HighAfterBits, 1200, nullptr, 72, 72, 72},
    {"Faac SLH", 64, kNone, {890, 1490}, {890, 1490}, true,
     {255, 100}, {595, 100}, {595, 100}, {255, 100}, false, PwmEnd::HighAfterBits, 865, nullptr, 0, 0, 0},
    {"KingGates Stylo4k", 53, {1920, 2480}, {520, 1080}, kNone, false,
     {1100, 280}, {400, 140}, {400, 140}, {1100, 280}, false, PwmEnd::LowAfterBits, 3300, nullptr, 89, 89, 89},
    {"Scher-Khan", 64, kNone, {1350, 1650}, {1350, 1650}, true,
     {750, 150}, {750, 150}, {1100, 150}, {1100, 150}, false, PwmEnd::HighAfterBits, 1400, nullptr, 35, 64, 0},
};
/* clang-format on */

constexpr std::size_t kPwmCount = sizeof(kPwmSpecs) / sizeof(kPwmSpecs[0]);

enum PwmStep : uint8_t {
    PwmIdle = 0,
    PwmStartHigh,
    PwmStartLow,
    PwmBitFirst,
    PwmBitSecond,
};

/* A bit whose two halves have different nominal lengths must show that difference. */
bool ordered(const Pulse& first_nominal, const Pulse& second_nominal, uint32_t first, uint32_t second) {
    if (first_nominal.nominal < second_nominal.nominal) {
        return first < second;
    }
    if (first_nominal.nominal > second_nominal.nominal) {
        return first > second;
    }
    return true;
}

bool match_bit(const PwmSpec& spec, uint32_t first, uint32_t second, bool& bit) {
    if (spec.loose_second) {
        const bool second_ok = spec.zero_second.fits(second) || spec.one_second.fits(second);
        if (second_ok && spec.zero_first.fits(first)) {
            bit = false;
            return true;
        }
        if (second_ok && spec.one_first.fits(first)) {
            bit = true;
            return true;
        }
        return false;
    }
    if (spec.zero_first.fits(first) && spec.zero_second.fits(second) &&
        ordered(spec.zero_first, spec.zero_second, first, second)) {
        bit = false;
        return true;
    }
    if (spec.one_first.fits(first) && spec.one_second.fits(second) &&
        ordered(spec.one_first, spec.one_second, first, second)) {
        bit = true;
        return true;
    }
    return false;
}

bool match_first_only(const PwmSpec& spec, uint32_t first, bool& bit) {
    const bool zero = spec.zero_first.fits(first);
    const bool one = spec.one_first.fits(first);
    if (zero && one) {
        const auto distance = [first](uint32_t nominal) {
            return first > nominal ? first - nominal : nominal - first;
        };
        bit = distance(spec.one_first.nominal) < distance(spec.zero_first.nominal);
        return true;
    }
    bit = one;
    return zero || one;
}

/* Two-level protocols: the shorter half of each bit is one base pulse. */
bool two_level(const PwmSpec& spec) {
    return spec.zero_first.nominal == spec.one_second.nominal &&
           spec.zero_second.nominal == spec.one_first.nominal &&
           spec.zero_first.nominal != spec.zero_second.nominal;
}

} // namespace

/* ---- Manchester --------------------------------------------------------- */

void ManchesterState::set(Position position) {
    position_ = position;
}

bool ManchesterState::advance(bool high, bool is_long, bool& emitted, bool& bit) {
    emitted = false;
    switch (position_) {
        case Position::MiddleOfOne:
            /* The second half of a one is high. */
            if (high && !is_long) {
                position_ = Position::StartOfOne;
                return true;
            }
            if (high && is_long) {
                position_ = Position::MiddleOfZero;
                emitted = true;
                bit = false;
                return true;
            }
            break;
        case Position::StartOfOne:
            if (!high && !is_long) {
                position_ = Position::MiddleOfOne;
                emitted = true;
                bit = true;
                return true;
            }
            break;
        case Position::MiddleOfZero:
            /* The second half of a zero is low. */
            if (!high && !is_long) {
                position_ = Position::StartOfZero;
                return true;
            }
            if (!high && is_long) {
                position_ = Position::MiddleOfOne;
                emitted = true;
                bit = true;
                return true;
            }
            break;
        case Position::StartOfZero:
            if (high && !is_long) {
                position_ = Position::MiddleOfZero;
                emitted = true;
                bit = false;
                return true;
            }
            break;
    }
    position_ = Position::MiddleOfOne;
    return false;
}

/* ---- Dispatcher --------------------------------------------------------- */

StreamDecoders::StreamDecoders() {
    static_assert(kPwmCount <= kMaxPwm, "raise StreamDecoders::kMaxPwm");
}

void StreamDecoders::reset() {
    pwm_ = {};
    megacode_ = {};
    chamberlain_ = {};
    intertechno_ = {};
    marantec_ = {};
    came_twee_ = {};
    power_smart_ = {};
    came_atomo_ = {};
    somfy_ = {};
}

std::size_t StreamDecoders::feed(bool high,
                                 uint32_t length_us,
                                 std::array<DecodedKey, kMaxKeysPerPulse>& keys) {
    std::size_t found = 0;
    DecodedKey key;
    const auto keep = [&](bool decoded) {
        if (decoded && found < keys.size()) {
            keys[found++] = key;
        }
    };

    for (std::size_t index = 0; index < kPwmCount; ++index) {
        keep(feed_pwm(kPwmSpecs[index], pwm_[index], high, length_us, key));
    }
    keep(feed_megacode(high, length_us, key));
    keep(feed_chamberlain(high, length_us, key));
    keep(feed_intertechno(high, length_us, key));
    keep(feed_marantec(high, length_us, key));
    keep(feed_came_twee(high, length_us, key));
    keep(feed_power_smart(high, length_us, key));
    keep(feed_came_atomo(high, length_us, key));
    keep(feed_somfy(high, length_us, key));
    return found;
}

/* ---- Table-driven pulse-width protocols --------------------------------- */

bool StreamDecoders::feed_pwm(const PwmSpec& spec,
                              PwmState& state,
                              bool high,
                              uint32_t length,
                              DecodedKey& key) {
    bool reprocess = false;
    bool decoded = pwm_step(spec, state, high, length, key, reprocess);
    if (reprocess) {
        /* The pulse that ended or broke a frame may be the header of the next one. */
        bool ignored = false;
        decoded = pwm_step(spec, state, high, length, key, ignored) || decoded;
    }
    return decoded;
}

bool StreamDecoders::pwm_step(const PwmSpec& spec,
                              PwmState& state,
                              bool high,
                              uint32_t length,
                              DecodedKey& key,
                              bool& reprocess) {
    const auto begin_bits = [&state] {
        state.step = PwmBitFirst;
        state.data = 0;
        state.extra = 0;
        state.count = 0;
        state.short_total = 0;
        state.short_count = 0;
    };
    const auto restart = [&state, &reprocess] {
        state.step = PwmIdle;
        reprocess = true;
    };
    const uint32_t total_min = spec.total_min != 0 ? spec.total_min : spec.bits;
    const uint32_t total_max = spec.total_max != 0 ? spec.total_max : spec.bits;
    const auto add_bit = [&spec, &state](bool bit) {
        /* Bits past the key length go to the extra field. */
        if (state.count < spec.bits) {
            state.data = (state.data << 1) | (bit ? 1U : 0U);
        }
        else {
            state.extra = (state.extra << 1) | (bit ? 1U : 0U);
        }
        ++state.count;
    };
    const auto finish = [&spec, &state, &key, total_min, total_max]() {
        if (state.count < total_min || state.count > total_max ||
            (spec.validate && !spec.validate(state.data))) {
            return false;
        }
        key.protocol = spec.name;
        key.bits = spec.report_bits != 0 ? spec.report_bits : state.count;
        key.key = state.data;
        /* Only frames that are always longer than the key carry meaningful extra bits. */
        const bool long_frame = total_min > spec.bits;
        key.extra = long_frame ? state.extra : 0;
        key.extra_bits = long_frame ? state.count - spec.bits : 0;
        const uint32_t nominal = std::min(spec.zero_first.nominal, spec.zero_second.nominal);
        key.te_us = (two_level(spec) && state.short_count > 0)
                        ? static_cast<uint32_t>(state.short_total / state.short_count)
                        : nominal;
        return true;
    };

    switch (state.step) {
        case PwmIdle:
            if (spec.gap_low.set()) {
                if (!high && spec.gap_low.has(length)) {
                    if (spec.start_high.set()) {
                        state.step = PwmStartHigh;
                    }
                    else if (spec.start_low.set()) {
                        state.step = PwmStartLow;
                    }
                    else {
                        begin_bits();
                    }
                }
            }
            else if (high && spec.start_high.has(length)) {
                if (spec.start_low.set()) {
                    state.step = PwmStartLow;
                }
                else {
                    begin_bits();
                }
            }
            return false;

        case PwmStartHigh:
            if (high && spec.start_high.has(length)) {
                if (spec.start_low.set()) {
                    state.step = PwmStartLow;
                }
                else {
                    begin_bits();
                }
            }
            else {
                restart();
            }
            return false;

        case PwmStartLow:
            if (!high && spec.start_low.has(length)) {
                begin_bits();
            }
            else {
                restart();
            }
            return false;

        case PwmBitFirst: {
            if (spec.end == PwmEnd::LowAfterBits && !high && length >= spec.end_min) {
                const bool decoded = finish();
                restart();
                return decoded;
            }
            if (spec.end == PwmEnd::HighAfterBits && high && length >= spec.end_min) {
                const bool decoded = finish();
                restart();
                return decoded;
            }
            if (high != spec.high_first || state.count > total_max) {
                restart();
                return false;
            }
            state.first = length;
            state.step = PwmBitSecond;
            return false;
        }

        case PwmBitSecond: {
            if (high == spec.high_first) {
                restart();
                return false;
            }
            if (!high && length >= spec.end_min) {
                if (spec.end == PwmEnd::LowCutsLastBit) {
                    bool bit = false;
                    const bool matched = match_first_only(spec, state.first, bit);
                    if (matched) {
                        add_bit(bit);
                    }
                    const bool decoded = matched && finish();
                    restart();
                    return decoded;
                }
                if (spec.end == PwmEnd::LowAfterTrailingPulse) {
                    const bool decoded = finish();
                    restart();
                    return decoded;
                }
            }
            bool bit = false;
            if (!match_bit(spec, state.first, length, bit)) {
                restart();
                return false;
            }
            add_bit(bit);
            state.short_total += std::min(state.first, length);
            ++state.short_count;
            state.step = PwmBitFirst;
            return false;
        }

        default:
            state.step = PwmIdle;
            return false;
    }
}

/* ---- MegaCode ----------------------------------------------------------- */

/*
 * 24 bits, each a 1 ms pulse placed early (0) or late (1) in a 6 ms slot. The
 * low before a pulse therefore depends on the previous bit as well.
 */
bool StreamDecoders::feed_megacode(bool high, uint32_t length, DecodedKey& key) {
    enum : uint8_t { Idle, Start, WaitLow, WaitHigh };
    auto& state = megacode_;

    for (int pass = 0; pass < 2; ++pass) {
        switch (state.step) {
            case Idle:
                if (!high && length >= 9600 && length <= 16400) {
                    state.step = Start;
                }
                return false;
            case Start:
                if (high && near(length, 1000, 300)) {
                    state.data = 1;
                    state.count = 1;
                    state.last_bit = true;
                    state.step = WaitLow;
                    return false;
                }
                state.step = Idle;
                continue;
            case WaitLow:
                if (!high && length >= 10000) {
                    const bool decoded = state.count == 24;
                    if (decoded) {
                        key.protocol = "MegaCode";
                        key.bits = 24;
                        key.key = state.data;
                        key.te_us = 1000;
                    }
                    state.step = Idle;
                    if (decoded) {
                        /* The closing gap may also open the next frame. */
                        if (length <= 16400) {
                            state.step = Start;
                        }
                        return true;
                    }
                    continue;
                }
                if (high || (!state.last_bit && length < 3000) || state.count >= 24) {
                    state.step = Idle;
                    continue;
                }
                state.last = state.last_bit ? length : length - 3000;
                state.step = WaitHigh;
                return false;
            case WaitHigh: {
                bool bit = false;
                if (high && near(length, 1000, 300) && near(state.last, 5000, 1000)) {
                    bit = true;
                }
                else if (high && near(length, 1000, 300) && near(state.last, 2000, 400)) {
                    bit = false;
                }
                else {
                    state.step = Idle;
                    continue;
                }
                state.data = (state.data << 1) | (bit ? 1U : 0U);
                ++state.count;
                state.last_bit = bit;
                state.step = WaitLow;
                return false;
            }
            default:
                state.step = Idle;
                return false;
        }
    }
    return false;
}

/* ---- Chamberlain Code --------------------------------------------------- */

namespace {

constexpr uint64_t kChamberlainStop = 0x1;
constexpr uint64_t kChamberlainOne = 0x3;
constexpr uint64_t kChamberlainZero = 0x7;

/*
 * The frame is a row of 4 ms symbols: stop, one or zero. Stop symbols sit at
 * fixed places that depend on whether the remote has 7, 8 or 9 DIP switches.
 */
bool chamberlain_parse(uint64_t symbols, uint32_t symbol_count, uint64_t& data, uint32_t& bits) {
    if (symbol_count > 11) {
        return false;
    }

    if ((symbols & 0xF000000FF0FULL) == 0x10000001101ULL) {
        bits = 7;
        symbols &= ~0xF000000FF0FULL;
        symbols = (symbols >> 12) | ((symbols >> 4) & 0xF);
    }
    else if ((symbols & 0xF00000F00FULL) == 0x1000001001ULL) {
        bits = 8;
        symbols &= ~0xF00000F00FULL;
        /* The sixth switch does not exist on these remotes and reads as zero. */
        symbols = (symbols >> 4) | (kChamberlainZero << 8);
    }
    else if ((symbols & 0xF000000000FULL) == 0x10000000001ULL) {
        bits = 9;
        symbols &= ~0xF000000000FULL;
        symbols >>= 4;
    }
    else {
        return false;
    }

    data = 0;
    for (uint32_t bit = 0; bit < bits; ++bit) {
        const uint64_t symbol = symbols & 0xF;
        if (symbol == kChamberlainOne) {
            data |= 1ULL << bit;
        }
        else if (symbol != kChamberlainZero) {
            return false;
        }
        symbols >>= 4;
    }
    return true;
}

} // namespace

bool StreamDecoders::feed_chamberlain(bool high, uint32_t length, DecodedKey& key) {
    enum : uint8_t { Idle, Start, WaitLow, WaitHigh };
    auto& state = chamberlain_;

    for (int pass = 0; pass < 2; ++pass) {
        switch (state.step) {
            case Idle:
                if (!high && length >= 35000 && length <= 43000) {
                    state.step = Start;
                }
                return false;
            case Start:
                if (high && near(length, 1000, 250)) {
                    state.data = kChamberlainStop;
                    state.count = 1;
                    state.step = WaitLow;
                    return false;
                }
                state.step = Idle;
                continue;
            case WaitLow:
                if (!high && length > 5000) {
                    uint64_t data = 0;
                    uint32_t bits = 0;
                    const bool decoded = state.count >= 10 &&
                                         chamberlain_parse(state.data, state.count, data, bits);
                    state.step = Idle;
                    if (decoded) {
                        key.protocol = "Cham_Code";
                        key.bits = bits;
                        key.key = data;
                        key.te_us = 1000;
                        if (length >= 35000 && length <= 43000) {
                            state.step = Start;
                        }
                        return true;
                    }
                    continue;
                }
                if (high || state.count > 11) {
                    state.step = Idle;
                    continue;
                }
                state.last = length;
                state.step = WaitHigh;
                return false;
            case WaitHigh: {
                uint64_t symbol = 0;
                if (high && near(state.last, 3000, 450) && near(length, 1000, 250)) {
                    symbol = kChamberlainStop;
                }
                else if (high && near(state.last, 2000, 350) && near(length, 2000, 350)) {
                    symbol = kChamberlainOne;
                }
                else if (high && near(state.last, 1000, 250) && near(length, 3000, 450)) {
                    symbol = kChamberlainZero;
                }
                else {
                    state.step = Idle;
                    continue;
                }
                state.data = (state.data << 4) | symbol;
                ++state.count;
                state.step = WaitLow;
                return false;
            }
            default:
                state.step = Idle;
                return false;
        }
    }
    return false;
}

/* ---- Intertechno V3 ----------------------------------------------------- */

/*
 * Every bit is two short pulses; which of the two lows after them is the long
 * one tells the bit. 32 bits, or 36 for a dimmer command.
 */
bool StreamDecoders::feed_intertechno(bool high, uint32_t length, DecodedKey& key) {
    enum : uint8_t { Idle, Sync, SyncLow, PulseA, GapA, PulseB, GapB };
    constexpr uint32_t kShort = 275;
    constexpr uint32_t kLong = 1375;
    auto& state = intertechno_;

    for (int pass = 0; pass < 2; ++pass) {
        switch (state.step) {
            case Idle:
                if (!high && length >= 7925 && length <= 12425) {
                    state.step = Sync;
                }
                return false;
            case Sync:
                if (high && near(length, kShort, 150)) {
                    state.step = SyncLow;
                    return false;
                }
                state.step = Idle;
                continue;
            case SyncLow:
                if (!high && near(length, kShort * 10, 450)) {
                    state.data = 0;
                    state.count = 0;
                    state.step = PulseA;
                    return false;
                }
                state.step = Idle;
                continue;
            case PulseA:
                if (high && near(length, kShort, 150)) {
                    state.step = GapA;
                    return false;
                }
                state.step = Idle;
                continue;
            case GapA:
                if (!high && length >= kShort * 11) {
                    const bool decoded = state.count == 32 || state.count == 36;
                    if (decoded) {
                        key.protocol = "Intertechno_V3";
                        key.bits = state.count;
                        key.key = state.data;
                        key.te_us = kShort;
                    }
                    /* The gap that closes a frame opens the next one. */
                    state.step = Sync;
                    return decoded;
                }
                if (high || state.count >= 36) {
                    state.step = Idle;
                    continue;
                }
                state.last = length;
                state.step = PulseB;
                return false;
            case PulseB: {
                if (!high || !near(length, kShort, 150)) {
                    state.step = Idle;
                    continue;
                }
                bool bit = false;
                if (near(state.last, kShort, 150)) {
                    bit = false;
                }
                else if (near(state.last, kLong, 300)) {
                    bit = true;
                }
                else {
                    state.step = Idle;
                    continue;
                }
                state.data = (state.data << 1) | (bit ? 1U : 0U);
                ++state.count;
                state.step = GapB;
                return false;
            }
            case GapB:
                if (!high && (near(length, kShort, 150) || near(length, kLong, 300))) {
                    state.step = PulseA;
                    return false;
                }
                state.step = Idle;
                continue;
            default:
                state.step = Idle;
                return false;
        }
    }
    return false;
}

/* ---- Manchester-coded protocols ----------------------------------------- */

/* Marantec: 49 bits at 1000 us per half bit, after a 10 ms low. The first bit is always 1. */
bool StreamDecoders::feed_marantec(bool high, uint32_t length, DecodedKey& key) {
    auto& state = marantec_;

    for (int pass = 0; pass < 2; ++pass) {
        if (!state.active) {
            if (!high && near(length, 10000, 1600)) {
                state.active = true;
                state.manchester.set(ManchesterState::Position::MiddleOfOne);
                state.data = 1;
                state.count = 1;
            }
            return false;
        }

        if (!high && length >= 4200) {
            const bool decoded = state.count == 49;
            if (decoded) {
                key.protocol = "Marantec";
                key.bits = 49;
                key.key = state.data;
                key.te_us = 1000;
            }
            state.active = false;
            if (decoded) {
                /* Let the same low open the next frame. */
                DecodedKey ignored;
                feed_marantec(high, length, ignored);
                return true;
            }
            continue;
        }

        const bool is_short = near(length, 1000, 300);
        const bool is_long = near(length, 2000, 350);
        bool emitted = false;
        bool bit = false;
        if ((!is_short && !is_long) || !state.manchester.advance(high, is_long, emitted, bit) ||
            state.count > 49) {
            state.active = false;
            continue;
        }
        if (emitted) {
            state.data = (state.data << 1) | (bit ? 1U : 0U);
            ++state.count;
        }
        return false;
    }
    return false;
}

/* CAME TWEE: 54 bits at 500 us per half bit, after a 51 ms low. Bits are sent inverted. */
bool StreamDecoders::feed_came_twee(bool high, uint32_t length, DecodedKey& key) {
    auto& state = came_twee_;

    for (int pass = 0; pass < 2; ++pass) {
        if (!state.active) {
            if (!high && near(length, 51000, 5000)) {
                state.active = true;
                state.manchester.set(ManchesterState::Position::StartOfZero);
                state.data = 0;
                state.count = 0;
            }
            return false;
        }

        if (!high && length >= 2250) {
            const bool decoded = state.count == 54;
            if (decoded) {
                key.protocol = "CAME TWEE";
                key.bits = 54;
                key.key = state.data;
                /*
                 * A press sends the same serial under fifteen different masks. Report
                 * them all as the first one, so they count as repeats of one key.
                 */
                if ((state.data >> 32) == kCameTweePrefix && (state.data & 0xF) < 15) {
                    const auto serial = static_cast<uint32_t>(state.data) ^ kCameTweeMasks[state.data & 0xF];
                    key.key = (kCameTweePrefix << 32) | (serial ^ kCameTweeMasks[0]);
                }
                key.te_us = 500;
            }
            state.active = false;
            if (decoded) {
                DecodedKey ignored;
                feed_came_twee(high, length, ignored);
                return true;
            }
            continue;
        }

        const bool is_short = near(length, 500, 250);
        const bool is_long = !is_short && near(length, 1000, 250);
        bool emitted = false;
        bool bit = false;
        if ((!is_short && !is_long) || !state.manchester.advance(high, is_long, emitted, bit) ||
            state.count > 54) {
            state.active = false;
            continue;
        }
        if (emitted) {
            state.data = (state.data << 1) | (bit ? 0U : 1U);
            ++state.count;
        }
        return false;
    }
    return false;
}

/*
 * Power Smart: a continuous inverted Manchester stream at 225 us per half bit.
 * A frame is recognised by its two fixed bytes and its built-in checks in a
 * sliding 64-bit window.
 */
bool StreamDecoders::feed_power_smart(bool high, uint32_t length, DecodedKey& key) {
    auto& state = power_smart_;

    const bool is_short = near(length, 225, 100);
    const bool is_long = !is_short && near(length, 450, 200);
    if (!is_short && !is_long) {
        state.data = 0;
        state.manchester.set(ManchesterState::Position::MiddleOfOne);
        return false;
    }

    bool emitted = false;
    bool bit = false;
    /* A pulse that does not fit realigns the bit clock and keeps the window. */
    if (!state.manchester.advance(high, is_long, emitted, bit) || !emitted) {
        return false;
    }
    state.data = (state.data << 1) | (bit ? 0U : 1U);

    const uint64_t packet = state.data;
    if ((packet & 0xFF000000FF000000ULL) != 0xFD000000AA000000ULL) {
        return false;
    }
    /* The second half repeats the first, inverted, and the low byte is a check on byte 4. */
    const auto data_1 = static_cast<uint32_t>((packet >> 40) & 0xFFFF);
    const auto data_2 = static_cast<uint32_t>((~packet >> 8) & 0xFFFF);
    const auto data_3 = static_cast<uint8_t>(packet >> 32);
    const auto data_4 = static_cast<uint8_t>(((~packet) & 0xFF) - 1);
    if (data_1 != data_2 || data_3 != data_4) {
        return false;
    }

    key.protocol = "Power Smart";
    key.bits = 64;
    key.key = packet;
    key.te_us = 225;
    state.data = 0;
    return true;
}

/* ---- Rolling codes with Manchester coding -------------------------------- */

/*
 * CAME Atomo: 62 bits at 600 us per half bit, after a 72 ms low. Bits are sent
 * inverted and the first one is always 0, its first half hidden in that low.
 */
bool StreamDecoders::feed_came_atomo(bool high, uint32_t length, DecodedKey& key) {
    auto& state = came_atomo_;

    for (int pass = 0; pass < 2; ++pass) {
        if (!state.active) {
            if (!high && near(length, 72000, 10000)) {
                state.active = true;
                state.manchester.set(ManchesterState::Position::MiddleOfOne);
                state.data = 0;
                state.count = 1;
            }
            return false;
        }

        if (!high && length >= 2650) {
            const bool decoded = state.count == 62;
            if (decoded) {
                key.protocol = "CAME Atomo";
                key.bits = 62;
                key.key = state.data;
                key.te_us = 600;
            }
            state.active = false;
            if (decoded) {
                /* Let the same low open the next frame. */
                DecodedKey ignored;
                feed_came_atomo(high, length, ignored);
                return true;
            }
            continue;
        }

        const bool is_short = near(length, 600, 250);
        const bool is_long = !is_short && near(length, 1200, 250);
        bool emitted = false;
        bool bit = false;
        if ((!is_short && !is_long) || !state.manchester.advance(high, is_long, emitted, bit) ||
            state.count > 62) {
            state.active = false;
            continue;
        }
        if (emitted) {
            state.data = (state.data << 1) | (bit ? 0U : 1U);
            ++state.count;
        }
        return false;
    }
    return false;
}

namespace {

/* Somfy frames are lightly scrambled: every byte is XORed with the one before it. */
bool somfy_checksum_ok(uint64_t data) {
    const uint64_t plain = data ^ (data >> 8);
    const uint64_t without_checksum = plain & 0xFFF0FFFFFFFFFFULL;
    uint8_t checksum = 0;
    for (int shift = 0; shift < 56; shift += 8) {
        checksum ^= static_cast<uint8_t>(without_checksum >> shift) ^
                    static_cast<uint8_t>(without_checksum >> (shift + 4));
    }
    return ((plain >> 40) & 0xF) == (checksum & 0xF);
}

} // namespace

/*
 * Somfy Telis (56 bits) and Keytis (80 bits, of which the first 56 are the
 * key): Manchester at 640 us per half bit, after two or more 2.5 ms sync
 * pulses and one 4.5 ms pulse.
 */
bool StreamDecoders::feed_somfy(bool high, uint32_t length, DecodedKey& key) {
    enum : uint8_t { Idle, SyncLow, SyncHigh, Data };
    auto& state = somfy_;
    const bool sync_pulse = near(length, 2560, 1000);

    for (int pass = 0; pass < 2; ++pass) {
        switch (state.step) {
            case Idle:
                if (high && sync_pulse) {
                    state.step = SyncLow;
                    state.header_count = 1;
                }
                return false;
            case SyncLow:
                if (!high && sync_pulse) {
                    state.step = SyncHigh;
                    return false;
                }
                state.step = Idle;
                continue;
            case SyncHigh:
                if (high && sync_pulse) {
                    ++state.header_count;
                    state.step = SyncLow;
                    return false;
                }
                if (high && state.header_count > 1 && near(length, 4480, 1000)) {
                    /* The long pulse ends where the second half of a zero would. */
                    state.manchester.set(ManchesterState::Position::MiddleOfZero);
                    state.data = 0;
                    state.extra = 0;
                    state.count = 0;
                    state.step = Data;
                    return false;
                }
                state.step = Idle;
                continue;
            case Data: {
                if (!high && length >= 1530) {
                    const bool telis = state.count == 56;
                    const bool keytis = state.count == 80;
                    state.step = Idle;
                    if ((telis || keytis) && somfy_checksum_ok(state.data)) {
                        key.protocol = telis ? "Somfy Telis" : "Somfy Keytis";
                        key.bits = state.count;
                        key.key = state.data;
                        key.extra = keytis ? state.extra : 0;
                        key.extra_bits = keytis ? 24 : 0;
                        key.te_us = 640;
                        return true;
                    }
                    return false;
                }

                const bool is_short = near(length, 640, 250);
                const bool is_long = !is_short && near(length, 1280, 250);
                bool emitted = false;
                bool bit = false;
                if ((!is_short && !is_long) || !state.manchester.advance(high, is_long, emitted, bit) ||
                    state.count > 80) {
                    state.step = Idle;
                    continue;
                }
                if (emitted) {
                    /* A Keytis frame carries 24 more bits after the key. */
                    if (state.count < 56) {
                        state.data = (state.data << 1) | (bit ? 1U : 0U);
                    }
                    else {
                        state.extra = (state.extra << 1) | (bit ? 1U : 0U);
                    }
                    ++state.count;
                }
                return false;
            }
            default:
                state.step = Idle;
                return false;
        }
    }
    return false;
}

} // namespace model
