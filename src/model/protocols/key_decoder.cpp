/*
 * SPDX-License-Identifier: MIT
 */

#include "protocols/key_decoder.h"

#include "protocols/rolling_codes.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace model {
namespace {

/* A low period at least this long separates two frames. */
constexpr uint32_t kGapUs = 3500;
/* Anything shorter is a glitch, anything longer is not a data pulse. */
constexpr uint32_t kMinPulseUs = 80;
constexpr uint32_t kMaxPulseUs = 3000;
constexpr std::size_t kMinPairs = 9;
constexpr std::size_t kMaxPairs = 64;
/* How much the bit period may vary inside one frame, relative to its mean. */
constexpr double kMaxPeriodSpread = 0.40;
/* A key counts as repeated when it is seen again within this time. */
constexpr uint64_t kRepeatWindowUs = 500000;

struct PairStats {
    double short_mean{0};
    double long_mean{0};
    bool distinct{true};
};

/* Relative spread (max - min) / mean of the bit periods under one layout. */
double period_spread(const std::vector<uint32_t>& frame, std::size_t pairs, std::size_t offset) {
    uint32_t smallest = UINT32_MAX;
    uint32_t largest = 0;
    uint64_t total = 0;
    for (std::size_t pair = 0; pair < pairs; ++pair) {
        const uint32_t period = frame[2 * pair + offset] + frame[2 * pair + offset + 1];
        smallest = std::min(smallest, period);
        largest = std::max(largest, period);
        total += period;
    }
    const double mean = static_cast<double>(total) / static_cast<double>(pairs);
    return mean > 0 ? static_cast<double>(largest - smallest) / mean : 1.0;
}

PairStats pair_stats(const std::vector<uint32_t>& frame, std::size_t pairs, std::size_t offset) {
    PairStats stats;
    uint64_t short_total = 0;
    uint64_t long_total = 0;
    for (std::size_t pair = 0; pair < pairs; ++pair) {
        const uint32_t first = frame[2 * pair + offset];
        const uint32_t second = frame[2 * pair + offset + 1];
        const uint32_t shorter = std::min(first, second);
        const uint32_t longer = std::max(first, second);
        /* Each bit must be clearly one short and one long pulse. */
        if (static_cast<double>(longer) < 1.4 * static_cast<double>(shorter)) {
            stats.distinct = false;
        }
        short_total += shorter;
        long_total += longer;
    }
    stats.short_mean = static_cast<double>(short_total) / static_cast<double>(pairs);
    stats.long_mean = static_cast<double>(long_total) / static_cast<double>(pairs);
    return stats;
}

} // namespace

bool DecodedKey::same_key(const DecodedKey& other) const {
    return bits == other.bits && key == other.key && extra == other.extra &&
           extra_bits == other.extra_bits && std::strcmp(protocol, other.protocol) == 0;
}

void KeyDecoder::reset() {
    frame_.clear();
    in_frame_ = false;
    stream_decoders_.reset();
    candidates_ = {};
    now_us_ = 0;
}

void KeyDecoder::abort_frame() {
    frame_.clear();
    in_frame_ = false;
}

bool KeyDecoder::feed(int32_t duration_us, DecodedKey& key) {
    if (duration_us == 0) {
        return false;
    }

    const bool high = duration_us > 0;
    const auto length = static_cast<uint32_t>(high ? duration_us : -static_cast<int64_t>(duration_us));
    now_us_ += length;

    bool confirmed = false;
    DecodedKey decoded;
    if (feed_frames(high, length, decoded) && confirm(decoded)) {
        key = decoded;
        confirmed = true;
    }

    std::array<DecodedKey, StreamDecoders::kMaxKeysPerPulse> stream_keys;
    const std::size_t found = stream_decoders_.feed(high, length, stream_keys);
    for (std::size_t index = 0; index < found; ++index) {
        if (confirm(stream_keys[index])) {
            key = stream_keys[index];
            confirmed = true;
        }
    }
    return confirmed;
}

bool KeyDecoder::confirm(const DecodedKey& decoded) {
    /*
     * A rolling-code frame is long and rigidly structured, so one is enough,
     * and it must not be missed: a remote may send a code only once or twice.
     */
    if (is_rolling_protocol(decoded.protocol)) {
        return true;
    }

    Candidate* slot = &candidates_[0];
    for (auto& candidate : candidates_) {
        if (candidate.valid && candidate.key.same_key(decoded)) {
            const bool repeated = now_us_ - candidate.seen_at_us <= kRepeatWindowUs;
            candidate.key = decoded;
            candidate.seen_at_us = now_us_;
            return repeated;
        }
        /* Otherwise reuse an empty slot, or failing that the oldest one. */
        if (!candidate.valid) {
            if (slot->valid) {
                slot = &candidate;
            }
        }
        else if (slot->valid && candidate.seen_at_us < slot->seen_at_us) {
            slot = &candidate;
        }
    }
    slot->key = decoded;
    slot->seen_at_us = now_us_;
    slot->valid = true;
    return false;
}

bool KeyDecoder::feed_frames(bool high, uint32_t length, DecodedKey& key) {
    if (!high && length >= kGapUs) {
        /* A gap closes the current frame and opens the next one. */
        const bool decoded = in_frame_ && classify(key);
        frame_.clear();
        in_frame_ = true;
        return decoded;
    }

    if (!in_frame_) {
        return false;
    }
    /* Inside a frame, highs sit at even positions and lows at odd ones. */
    const bool expect_high = frame_.size() % 2 == 0;
    if (high != expect_high || length < kMinPulseUs || length > kMaxPulseUs ||
        frame_.size() >= 2 * kMaxPairs + 1) {
        abort_frame();
        return false;
    }
    frame_.push_back(length);
    return false;
}


bool KeyDecoder::classify(DecodedKey& key) const {
    /* A complete frame is high, low, ..., high: the final low was the gap. */
    if (frame_.size() % 2 == 0 || frame_.size() < 2 * kMinPairs + 1) {
        return false;
    }

    const std::size_t pairs = (frame_.size() - 1) / 2;
    /* Bits are either (high, low) pairs from the start, or (low, high) pairs after a start pulse. */
    const double high_first_spread = period_spread(frame_, pairs, 0);
    const double low_first_spread = period_spread(frame_, pairs, 1);

    if (high_first_spread <= low_first_spread) {
        return (high_first_spread <= kMaxPeriodSpread && classify_high_first(key)) ||
               (low_first_spread <= kMaxPeriodSpread && classify_low_first(key));
    }
    return (low_first_spread <= kMaxPeriodSpread && classify_low_first(key)) ||
           (high_first_spread <= kMaxPeriodSpread && classify_high_first(key));
}

bool KeyDecoder::classify_high_first(DecodedKey& key) const {
    const std::size_t pairs = (frame_.size() - 1) / 2;
    if (pairs != 24 && pairs != 9) {
        return false;
    }

    const PairStats stats = pair_stats(frame_, pairs, 0);
    const double ratio = stats.short_mean > 0 ? stats.long_mean / stats.short_mean : 0;
    /* Both protocols use a 1:3 pulse ratio. */
    if (!stats.distinct || ratio < 2.2 || ratio > 4.2) {
        return false;
    }

    uint64_t value = 0;
    for (std::size_t pair = 0; pair < pairs; ++pair) {
        value = (value << 1) | (frame_[2 * pair] > frame_[2 * pair + 1] ? 1U : 0U);
    }

    const double last_high = static_cast<double>(frame_.back());
    if (pairs == 24) {
        /* Princeton: 24 bits, then a short sync pulse before the gap. */
        if (last_high < 0.4 * stats.short_mean || last_high > 2.0 * stats.short_mean) {
            return false;
        }
        key.protocol = "Princeton";
        key.bits = 24;
    }
    else {
        /* Linear: the tenth bit's low period is the gap itself. */
        value = (value << 1) | (last_high > 2.0 * stats.short_mean ? 1U : 0U);
        key.protocol = "Linear";
        key.bits = 10;
    }
    key.key = value;
    key.te_us = static_cast<uint32_t>(stats.short_mean + 0.5);
    return true;
}

bool KeyDecoder::classify_low_first(DecodedKey& key) const {
    const std::size_t pairs = (frame_.size() - 1) / 2;
    if (pairs != 12 && pairs != 18 && pairs != 24 && pairs != 25) {
        return false;
    }

    const PairStats stats = pair_stats(frame_, pairs, 1);
    const double ratio = stats.short_mean > 0 ? stats.long_mean / stats.short_mean : 0;
    /*
     * CAME, Nice FLO and Ansonic use a 1:2 pulse ratio and open with one short
     * start pulse. GateTX has the same layout with a start pulse twice as long,
     * and is left to the stream decoders.
     */
    const double start = static_cast<double>(frame_.front());
    if (!stats.distinct || ratio < 1.4 || ratio > 2.8 || start < 0.4 * stats.short_mean ||
        start > 1.5 * stats.short_mean) {
        return false;
    }

    uint64_t value = 0;
    for (std::size_t pair = 0; pair < pairs; ++pair) {
        value = (value << 1) | (frame_[2 * pair + 1] > frame_[2 * pair + 2] ? 1U : 0U);
    }

    /*
     * They differ in speed: CAME is about 320 us per unit, Ansonic 555 us and
     * Nice FLO 700 us. Ansonic also sends its bits the other way up.
     */
    if (stats.short_mean < 450.0) {
        key.protocol = "CAME";
    }
    else if (stats.short_mean < 630.0 && pairs == 12) {
        key.protocol = "Ansonic";
        value ^= 0xFFF;
    }
    else if (stats.short_mean >= 630.0 && (pairs == 12 || pairs == 24)) {
        key.protocol = "Nice FLO";
    }
    else {
        return false;
    }
    key.bits = static_cast<uint32_t>(pairs);
    key.key = value;
    key.te_us = static_cast<uint32_t>(stats.short_mean + 0.5);
    return true;
}

} // namespace model
