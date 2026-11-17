/*
 * SPDX-License-Identifier: MIT
 */

#include "protocols/key_encoder.h"

#include <initializer_list>

namespace model {
namespace {

/* Frames sent for one button press, unless a protocol says otherwise. */
constexpr int kDefaultRepeats = 10;

class Train {
public:
    explicit Train(std::vector<int32_t>& pulses) : pulses_(pulses) {
        pulses_.clear();
    }

    void high(uint32_t duration_us) {
        add(static_cast<int32_t>(duration_us));
    }
    void low(uint32_t duration_us) {
        add(-static_cast<int32_t>(duration_us));
    }
    /* One half bit of Manchester code. */
    void half(bool is_high, uint32_t duration_us) {
        is_high ? high(duration_us) : low(duration_us);
    }
    bool ends_low() const {
        return !pulses_.empty() && pulses_.back() < 0;
    }
    /* Removes a trailing carrier-off period of exactly this length, if there is one. */
    void drop_trailing_low(uint32_t duration_us) {
        if (ends_low() && pulses_.back() == -static_cast<int32_t>(duration_us)) {
            pulses_.pop_back();
        }
        else if (ends_low() && pulses_.back() < -static_cast<int32_t>(duration_us)) {
            pulses_.back() += static_cast<int32_t>(duration_us);
        }
    }

private:
    void add(int32_t duration) {
        if (duration == 0) {
            return;
        }
        /* Neighbours of the same level are one pulse on the air. */
        if (!pulses_.empty() && ((pulses_.back() > 0) == (duration > 0))) {
            pulses_.back() += duration;
        }
        else {
            pulses_.push_back(duration);
        }
    }

    std::vector<int32_t>& pulses_;
};

bool bit_at(uint64_t key, uint32_t index) {
    return ((key >> index) & 1U) != 0;
}

bool one_of(uint32_t bits, std::initializer_list<uint32_t> allowed) {
    for (const uint32_t value : allowed) {
        if (bits == value) {
            return true;
        }
    }
    return false;
}

/* A bit as two pulses. Low-first protocols give (low, high), high-first ones (high, low). */
struct BitShape {
    uint32_t one_first;
    uint32_t one_second;
    uint32_t zero_first;
    uint32_t zero_second;
};

/* gap, start pulse, then every bit as (low, high). */
void low_first_frames(Train& train, uint32_t bits, uint64_t key, uint32_t gap, uint32_t start,
                      const BitShape& shape, int repeats) {
    for (int repeat = 0; repeat < repeats; ++repeat) {
        train.low(gap);
        train.high(start);
        for (uint32_t index = bits; index > 0; --index) {
            const bool bit = bit_at(key, index - 1);
            train.low(bit ? shape.one_first : shape.zero_first);
            train.high(bit ? shape.one_second : shape.zero_second);
        }
    }
    /* Close the last frame the way the next one would have. */
    train.low(gap);
}

/* Every bit as (high, low). The last bit's low is replaced by the gap when `last_low` is given. */
void high_first_bits(Train& train, uint32_t bits, uint64_t key, const BitShape& shape,
                     uint32_t last_low_one, uint32_t last_low_zero) {
    for (uint32_t index = bits; index > 0; --index) {
        const bool bit = bit_at(key, index - 1);
        const bool last = index == 1 && (last_low_one != 0 || last_low_zero != 0);
        train.high(bit ? shape.one_first : shape.zero_first);
        if (last) {
            train.low(bit ? last_low_one : last_low_zero);
        }
        else {
            train.low(bit ? shape.one_second : shape.zero_second);
        }
    }
}

/* Manchester bits. `one_low_first` picks the polarity: a 1 is low then high when set. */
void manchester_bits(Train& train, uint32_t bits, uint64_t key, uint32_t half_us, bool one_low_first,
                     bool skip_first_half) {
    for (uint32_t index = bits; index > 0; --index) {
        const bool bit = bit_at(key, index - 1);
        const bool low_first = bit == one_low_first;
        if (!(index == bits && skip_first_half)) {
            train.half(!low_first, half_us);
        }
        train.half(low_first, half_us);
    }
}

bool refuse(std::string& error, const std::string& protocol, uint32_t bits) {
    error = protocol + " " + std::to_string(bits) + " bit cannot be sent";
    return false;
}

} // namespace

bool encode_key(const std::string& protocol,
                uint32_t bits,
                uint64_t key,
                uint32_t te_us,
                std::vector<int32_t>& pulses,
                std::string& error) {
    error.clear();
    Train train(pulses);

    if (protocol == "Princeton" && bits == 24) {
        const uint32_t te = te_us != 0 ? te_us : 390;
        for (int repeat = 0; repeat < kDefaultRepeats; ++repeat) {
            high_first_bits(train, bits, key, {te * 3, te, te, te * 3}, 0, 0);
            train.high(te);
            train.low(te * 30);
        }
        return true;
    }
    if (protocol == "SMC5326" && bits == 25) {
        const uint32_t te = te_us != 0 ? te_us : 300;
        for (int repeat = 0; repeat < kDefaultRepeats; ++repeat) {
            high_first_bits(train, bits, key, {te * 3, te, te, te * 3}, 0, 0);
            train.high(te);
            train.low(te * 25);
        }
        return true;
    }
    if (protocol == "Linear" && bits == 10) {
        for (int repeat = 0; repeat < kDefaultRepeats; ++repeat) {
            high_first_bits(train, bits, key, {1500, 500, 500, 1500}, 500 * 42, 500 * 44);
        }
        return true;
    }
    if (protocol == "LinearDelta3" && bits == 8) {
        for (int repeat = 0; repeat < kDefaultRepeats; ++repeat) {
            high_first_bits(train, bits, key, {500, 3500, 2000, 2000}, 500 * 73, 500 * 70);
        }
        return true;
    }
    if (protocol == "Clemsa" && bits == 18) {
        for (int repeat = 0; repeat < kDefaultRepeats; ++repeat) {
            high_first_bits(train, bits, key, {2695, 385, 385, 2695}, 385 + 2695 * 7, 2695 + 2695 * 7);
        }
        return true;
    }
    if (protocol == "BETT" && bits == 18) {
        for (int repeat = 0; repeat < kDefaultRepeats; ++repeat) {
            high_first_bits(train, bits, key, {2000, 340, 340, 2000}, 340 + 2000 * 7, 2000 + 2000 * 7);
        }
        return true;
    }

    if (protocol == "CAME" && one_of(bits, {12, 18, 24, 25})) {
        /* The silent gap before a frame depends on the key length. */
        const uint32_t gap_units = bits == 24 ? 76 : (bits == 12 ? 47 : (bits == 25 ? 36 : 16));
        low_first_frames(train, bits, key, 320 * gap_units, 320, {640, 320, 320, 640}, kDefaultRepeats);
        return true;
    }
    if (protocol == "Nice FLO" && one_of(bits, {12, 24})) {
        low_first_frames(train, bits, key, 700 * 36, 700, {1400, 700, 700, 1400}, kDefaultRepeats);
        return true;
    }
    if (protocol == "Ansonic" && bits == 12) {
        low_first_frames(train, bits, key, 555 * 35, 555, {555, 1111, 1111, 555}, kDefaultRepeats);
        return true;
    }
    if (protocol == "Holtek" && bits == 40) {
        low_first_frames(train, bits, key, 430 * 36, 430, {870, 430, 430, 870}, kDefaultRepeats);
        return true;
    }
    if (protocol == "GateTX" && bits == 24) {
        low_first_frames(train, bits, key, 350 * 49, 700, {700, 350, 350, 700}, kDefaultRepeats);
        return true;
    }
    if (protocol == "Doitrand" && bits == 37) {
        low_first_frames(train, bits, key, 400 * 62, 700, {1100, 400, 400, 1100}, kDefaultRepeats);
        return true;
    }
    if (protocol == "Phoenix_V2" && bits == 52) {
        low_first_frames(train, bits, key, 427 * 60, 427 * 6, {427, 853, 853, 427}, kDefaultRepeats);
        return true;
    }

    if (protocol == "Dooya" && bits == 40) {
        for (int repeat = 0; repeat <= kDefaultRepeats; ++repeat) {
            /* The gap is trimmed so that it and the last bit's low always add up to the same length. */
            train.low(733 * 12 + (bit_at(key, 0) ? 733 : 366));
            if (repeat == kDefaultRepeats) {
                break;
            }
            train.high(366 * 13);
            train.low(733 * 2);
            high_first_bits(train, bits, key, {733, 366, 366, 733}, 0, 0);
        }
        return true;
    }
    if (protocol == "Hormann HSM" && bits == 44) {
        for (int repeat = 0; repeat < 20; ++repeat) {
            train.high(500 * 24);
            train.low(500);
            high_first_bits(train, bits, key, {1000, 500, 500, 1000}, 0, 0);
        }
        train.high(500 * 24);
        return true;
    }
    if ((protocol == "Nero Sketch" && bits == 40) || (protocol == "Nero Radio" && bits == 56)) {
        const bool sketch = bits == 40;
        const uint32_t te = sketch ? 330 : 200;
        for (int repeat = 0; repeat < kDefaultRepeats; ++repeat) {
            for (int pair = 0; pair < (sketch ? 47 : 49); ++pair) {
                train.high(te);
                train.low(te);
            }
            train.high(te * 4);
            train.low(te);
            if (sketch) {
                high_first_bits(train, bits, key, {te * 2, te, te, te * 2}, 0, 0);
                train.high(te * 3);
                train.low(te);
            }
            else {
                high_first_bits(train, bits, key, {te * 2, te, te, te * 2}, te * 37, te * 37);
            }
        }
        return true;
    }
    if (protocol == "Magellan" && bits == 32) {
        for (int repeat = 0; repeat < kDefaultRepeats; ++repeat) {
            train.high(200 * 4);
            train.low(200);
            for (int pair = 0; pair < 12; ++pair) {
                train.high(200);
                train.low(200);
            }
            train.high(200);
            train.low(400);
            train.high(400 * 3);
            train.low(400);
            high_first_bits(train, bits, key, {200, 400, 400, 200}, 0, 0);
            train.high(200);
            train.low(400 * 100);
        }
        return true;
    }
    if (protocol == "Honeywell" && bits == 48) {
        for (int repeat = 0; repeat < kDefaultRepeats; ++repeat) {
            train.low(160 * 3);
            high_first_bits(train, bits, key, {320, 160, 160, 320}, 0, 0);
            train.high(160 * 3);
        }
        return true;
    }

    if (protocol == "MegaCode" && bits == 24 && bit_at(key, 23)) {
        /*
         * One 1 ms pulse per bit, early (0) or late (1) in a 6 ms slot, so the
         * low before a pulse depends on the previous bit too.
         */
        for (int repeat = 0; repeat <= kDefaultRepeats; ++repeat) {
            train.low(bit_at(key, 0) ? 11000 : 14000);
            if (repeat == kDefaultRepeats) {
                break;
            }
            train.high(1000);
            for (uint32_t index = bits - 1; index > 0; --index) {
                const bool previous = bit_at(key, index);
                const bool current = bit_at(key, index - 1);
                train.low(previous ? (current ? 5000 : 2000) : (current ? 8000 : 5000));
                train.high(1000);
            }
        }
        return true;
    }
    if (protocol == "Cham_Code" && one_of(bits, {7, 8, 9})) {
        /* Symbols are four 1 ms slots: stop 0001, one 0011, zero 0111. */
        constexpr uint64_t kStop = 0x1;
        std::vector<uint64_t> symbols;
        const auto data_symbol = [key](uint32_t bit) -> uint64_t { return bit_at(key, bit) ? 0x3 : 0x7; };
        symbols.push_back(kStop);
        if (bits == 9) {
            for (uint32_t bit = 9; bit > 0; --bit) {
                symbols.push_back(data_symbol(bit - 1));
            }
        }
        else if (bits == 8) {
            /* Bit 2 is the switch these remotes do not have; it is not sent. */
            for (uint32_t bit = 8; bit > 3; --bit) {
                symbols.push_back(data_symbol(bit - 1));
            }
            symbols.push_back(kStop);
            symbols.push_back(data_symbol(1));
            symbols.push_back(data_symbol(0));
        }
        else {
            for (uint32_t bit = 7; bit > 1; --bit) {
                symbols.push_back(data_symbol(bit - 1));
            }
            symbols.push_back(kStop);
            symbols.push_back(kStop);
            symbols.push_back(data_symbol(0));
        }
        symbols.push_back(kStop);

        for (int repeat = 0; repeat <= kDefaultRepeats; ++repeat) {
            train.low(36000);
            if (repeat == kDefaultRepeats) {
                break;
            }
            for (const uint64_t symbol : symbols) {
                for (int slot = 3; slot >= 0; --slot) {
                    train.half(((symbol >> slot) & 1U) != 0, 1000);
                }
            }
        }
        return true;
    }
    if (protocol == "Intertechno_V3" && one_of(bits, {32, 36})) {
        for (int repeat = 0; repeat < kDefaultRepeats; ++repeat) {
            train.high(275);
            train.low(275 * 38);
            train.high(275);
            train.low(275 * 10);
            for (uint32_t index = bits; index > 0; --index) {
                const bool bit = bit_at(key, index - 1);
                /* In a dimmer command the ninth bit from the end is sent as two short lows. */
                const bool dim = bits == 36 && index == 9;
                train.high(275);
                train.low((bit && !dim) ? 1375 : 275);
                train.high(275);
                train.low((bit || dim) ? 275 : 1375);
            }
        }
        train.high(275);
        return true;
    }

    if (protocol == "Marantec" && bits == 49 && bit_at(key, 48)) {
        for (int repeat = 0; repeat < kDefaultRepeats; ++repeat) {
            /* The 10 ms low stands in for the first half of the leading 1. */
            train.drop_trailing_low(1000);
            train.low(10000);
            manchester_bits(train, bits, key, 1000, true, true);
        }
        train.drop_trailing_low(1000);
        train.low(10000);
        return true;
    }
    if (protocol == "CAME TWEE" && bits == 54 && (key >> 32) == kCameTweePrefix && (key & 0xF) < 15) {
        /* A press sends the same serial fifteen times, each under a different mask. */
        const auto serial = static_cast<uint32_t>(key) ^ kCameTweeMasks[key & 0xF];
        for (int cycle = 0; cycle < 2; ++cycle) {
            for (int parcel = 14; parcel >= 0; --parcel) {
                const uint64_t frame = (kCameTweePrefix << 32) | (serial ^ kCameTweeMasks[parcel]);
                manchester_bits(train, bits, frame, 500, false, false);
                train.drop_trailing_low(500);
                train.low(51000);
            }
        }
        return true;
    }
    if (protocol == "Power Smart" && bits == 64) {
        for (int packet = 0; packet < 8; ++packet) {
            manchester_bits(train, bits, key, 225, false, false);
        }
        train.drop_trailing_low(225);
        train.low(450 * 1111);
        return true;
    }

    pulses.clear();
    return refuse(error, protocol, bits);
}

} // namespace model
