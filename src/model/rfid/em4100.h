/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace model {

/* 125 kHz carrier: one field cycle lasts 8 microseconds. */
constexpr uint32_t kRfidCycleUs = 8;

/*
 * One 125 kHz key as a Flipper .rfid file holds it: a key type name and its
 * data bytes. Only the EM4100 family can be emulated; other types are kept so
 * their files can still be listed and shown.
 */
struct RfidKey {
    /* Flipper's name for the type, e.g. "EM4100" or "EM4100/32". */
    std::string type{"EM4100"};
    std::vector<uint8_t> data;
};

/* Size of an EM4100 ID: a version / customer byte and a 32-bit card number. */
constexpr std::size_t kEm4100DataSize = 5;
constexpr std::size_t kEm4100FrameBits = 64;

/*
 * Field cycles per bit for an EM4100 family type: 64 for "EM4100", 32 for
 * "EM4100/32", 16 for "EM4100/16". 0 when the type is not EM4100 at all.
 */
uint32_t em4100_cycles_per_bit(const std::string& type);

/* True when the key is an EM4100 family type with 5 data bytes. */
bool rfid_can_emulate(const RfidKey& key);

/*
 * The 64-bit EM4100 frame, first bit sent first: 9 header ones, ten rows of
 * four data bits each followed by an even parity bit, four even column parity
 * bits, and a stop zero.
 */
std::array<bool, kEm4100FrameBits> em4100_frame(const std::array<uint8_t, kEm4100DataSize>& id);

/*
 * The reverse of em4100_frame(): checks header, parity and stop bit. Used by
 * the tests, and later by a reader.
 */
bool em4100_decode(const std::array<bool, kEm4100FrameBits>& frame, std::array<uint8_t, kEm4100DataSize>& id);

/*
 * One frame as load levels, two half-bits per bit (Manchester): true means the
 * coil is loaded. Normally a 1 is "loaded, then unloaded" and a 0 the
 * opposite; `inverted` swaps that, for readers that expect the other polarity.
 */
std::vector<bool> em4100_half_bits(const std::array<uint8_t, kEm4100DataSize>& id, bool inverted);

/*
 * Turns the reader's demodulated pulse train back into EM4100 IDs.
 *
 * Input is signed durations in microseconds, as PulseCapture records them
 * (positive high, negative low). Each pulse is rounded to one or two
 * half-bits, which rebuilds the Manchester half-bit stream; both phases and
 * both polarities are tried, and a frame counts once its header, row and
 * column parity and stop bit check out. Pulses far from a half-bit length
 * restart the stream, so noise between tags does no harm.
 */
class Em4100Decoder {
public:
    explicit Em4100Decoder(uint32_t half_bit_us = 256);

    void feed(int32_t duration_us);
    /* The latest ID decoded since the last call, if any. */
    bool take(std::array<uint8_t, kEm4100DataSize>& id);
    void reset();

private:
    void try_decode();

    uint32_t half_bit_us_;
    std::vector<bool> halves_;
    /* Half-bits added since the last decoding attempt. */
    std::size_t new_halves_{0};
    bool have_id_{false};
    std::array<uint8_t, kEm4100DataSize> id_{};
};

/* "01 23 45 67 89" */
std::string rfid_hex(const std::vector<uint8_t>& data);

/*
 * The numbers printed on most EM4100 fobs and cards: the 32-bit card number
 * as ten decimal digits, and the "facility,card" pair taken from its bytes
 * (byte 2, then bytes 3-4). For 01 23 45 67 89 that is "0591751049" and
 * "069,26505".
 */
std::string em4100_card_number(const std::vector<uint8_t>& data);
std::string em4100_facility_card(const std::vector<uint8_t>& data);

} // namespace model
