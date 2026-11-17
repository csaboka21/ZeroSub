/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "protocols/decoded_key.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace model {

/*
 * Decoders for fixed-code protocols with a known, fixed timing. Each one is a
 * small state machine fed with every pulse, so they also handle signals that
 * have no silent gap between frames.
 *
 * Pulse-width protocols that only differ in their numbers share one
 * table-driven machine (PwmSpec); the rest have a machine of their own.
 * Timings follow the values the Flipper Zero firmware uses; a few tolerances
 * are wider, because edges timed by Linux jitter more than a microcontroller's.
 *
 * A decoder reports every frame that decodes; requiring a repeat before a key
 * is shown is left to the caller.
 */

/* Raw Manchester bit recovery. A raw 1 is a low half followed by a high half. */
class ManchesterState {
public:
    enum class Position {
        StartOfOne,
        MiddleOfOne,
        MiddleOfZero,
        StartOfZero,
    };

    void set(Position position);
    /*
     * Advances by one pulse. Returns false when the pulse does not fit, in
     * which case the position falls back to the middle of a one. `emitted` is
     * set when the pulse reached the middle of a bit, and `bit` is its raw value.
     */
    bool advance(bool high, bool is_long, bool& emitted, bool& bit);

private:
    Position position_{Position::MiddleOfOne};
};

struct PwmSpec;

class StreamDecoders {
public:
    StreamDecoders();
    void reset();
    /*
     * Feeds one pulse. Returns the number of keys written to `keys`
     * (at most kMaxKeysPerPulse).
     */
    static constexpr std::size_t kMaxKeysPerPulse = 4;
    std::size_t feed(bool high, uint32_t length_us, std::array<DecodedKey, kMaxKeysPerPulse>& keys);

private:
    static constexpr std::size_t kMaxPwm = 24;

    struct PwmState {
        uint8_t step{0};
        uint32_t first{0};
        uint64_t data{0};
        /* Bits received after the key is full. */
        uint64_t extra{0};
        uint32_t count{0};
        uint64_t short_total{0};
        uint32_t short_count{0};
    };

    struct SymbolState {
        uint8_t step{0};
        uint32_t last{0};
        uint64_t data{0};
        uint32_t count{0};
        bool last_bit{false};
    };

    struct SomfyState {
        uint8_t step{0};
        uint32_t header_count{0};
        ManchesterState manchester;
        uint64_t data{0};
        uint64_t extra{0};
        uint32_t count{0};
    };

    struct ManchesterDecoderState {
        bool active{false};
        ManchesterState manchester;
        uint64_t data{0};
        uint32_t count{0};
    };

    bool feed_pwm(const PwmSpec& spec, PwmState& state, bool high, uint32_t length, DecodedKey& key);
    bool pwm_step(const PwmSpec& spec, PwmState& state, bool high, uint32_t length, DecodedKey& key,
                  bool& reprocess);
    bool feed_megacode(bool high, uint32_t length, DecodedKey& key);
    bool feed_chamberlain(bool high, uint32_t length, DecodedKey& key);
    bool feed_intertechno(bool high, uint32_t length, DecodedKey& key);
    bool feed_marantec(bool high, uint32_t length, DecodedKey& key);
    bool feed_came_twee(bool high, uint32_t length, DecodedKey& key);
    bool feed_power_smart(bool high, uint32_t length, DecodedKey& key);
    bool feed_came_atomo(bool high, uint32_t length, DecodedKey& key);
    bool feed_somfy(bool high, uint32_t length, DecodedKey& key);

    std::array<PwmState, kMaxPwm> pwm_{};
    SymbolState megacode_;
    SymbolState chamberlain_;
    SymbolState intertechno_;
    ManchesterDecoderState marantec_;
    ManchesterDecoderState came_twee_;
    ManchesterDecoderState power_smart_;
    ManchesterDecoderState came_atomo_;
    SomfyState somfy_;
};

} // namespace model
