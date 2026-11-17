/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "protocols/decoded_key.h"
#include "protocols/stream_decoders.h"

#include <array>
#include <cstdint>
#include <vector>

namespace model {

/*
 * Decodes fixed-code remotes from a pulse train.
 *
 * Princeton (24 bit), Linear (10 bit), CAME (12, 18, 24, 25 bit), Nice FLO
 * (12, 24 bit) and Ansonic (12 bit) send a frame of pulse-width-coded bits
 * between long silent gaps. For these the decoder collects one frame at a time,
 * works out which of the two bit layouts it uses from the timing, and measures
 * the pulse length rather than assuming it.
 *
 * Every other protocol has fixed timing and is handled by StreamDecoders.
 *
 * A fixed-code key is only reported once the same frame has been seen twice
 * within half a second, which keeps noise from producing keys.
 *
 * Rolling-code frames are recognised too (see rolling_codes.h) and reported on
 * the first frame. They are not decrypted.
 */
class KeyDecoder {
public:
    void reset();
    /*
     * Feeds one duration in microseconds: positive for carrier on, negative for
     * carrier off. Returns true when `key` holds a confirmed key. A held button
     * keeps returning the same key once per repeated frame.
     */
    bool feed(int32_t duration_us, DecodedKey& key);

private:
    bool feed_frames(bool high, uint32_t length, DecodedKey& decoded);
    /* True when the same key was already seen a moment ago. */
    bool confirm(const DecodedKey& decoded);
    bool classify(DecodedKey& key) const;
    bool classify_high_first(DecodedKey& key) const;
    bool classify_low_first(DecodedKey& key) const;
    void abort_frame();

    /* Durations of the frame being collected: high, low, high, ... */
    std::vector<uint32_t> frame_;
    bool in_frame_{false};
    StreamDecoders stream_decoders_;

    /* Keys seen once, waiting for their repeat. Several protocols may be waiting at once. */
    struct Candidate {
        DecodedKey key;
        uint64_t seen_at_us{0};
        bool valid{false};
    };
    std::array<Candidate, 4> candidates_{};
    /* Time fed so far, in microseconds. */
    uint64_t now_us_{0};
};

} // namespace model
