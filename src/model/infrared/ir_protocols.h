/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace model {

/*
 * One decoded remote-control message. The protocol names, and the way a frame
 * is split into address and command, are the Flipper Zero's, so a message
 * means the same thing in a Flipper .ir file.
 */
struct IrMessage {
    /* Static string, e.g. "NECext". Null when empty. */
    const char* protocol{nullptr};
    uint32_t address{0};
    uint32_t command{0};

    bool same_as(const IrMessage& other) const;
};

/* What one burst of infrared turned out to be. */
enum class IrFrameKind {
    Unknown,
    Message,
    /* A "button still held" frame that carries no data of its own (NEC, Samsung). */
    Repeat,
};

/*
 * Decodes one frame: microsecond durations that start with a mark (carrier on)
 * and alternate mark, space, mark, ... A frame ends at the first long silence,
 * which is not part of it.
 */
IrFrameKind ir_decode_frame(const std::vector<uint32_t>& frame, IrMessage& message);

/* Carrier a protocol is sent on. */
struct IrCarrier {
    uint32_t frequency_hz{38000};
    float duty_cycle{0.33F};
};

/*
 * Builds what one press of a button sends: durations that start with a mark
 * and alternate, ending on a mark. Protocols that repeat a frame even for the
 * shortest press (SIRC three times, Pioneer twice) have the repeats and the
 * silences between them included.
 */
bool ir_encode(const std::string& protocol,
               uint32_t address,
               uint32_t command,
               std::vector<uint32_t>& timings,
               IrCarrier& carrier,
               std::string& error);

/* Width of a protocol's address and command in bits. False for an unknown protocol. */
bool ir_protocol_widths(const std::string& protocol, uint32_t& address_bits, uint32_t& command_bits);

/* The protocol's own static name string, or null when it is not one this app knows. */
const char* ir_protocol_name(const std::string& protocol);

/* One signal out of the collector: a decoded message, or the raw timings of something unknown. */
struct IrCapture {
    bool raw{false};
    IrMessage message;
    /* Raw only: starts and ends with a mark. */
    std::vector<uint32_t> timings;
};

/*
 * Turns the receiver's stream of marks and spaces into signals.
 *
 * A signal is everything between two long silences: for a held button that is
 * the frame and all of its repeats. Each frame is tried against the decoders
 * as soon as it ends. A signal in which nothing decoded comes out once, as raw
 * timings, when it is over.
 */
class IrCollector {
public:
    /* A silence at least this long ends a frame; no protocol has a longer gap inside a frame. */
    static constexpr uint32_t kFrameGapUs = 5500;
    /* A silence at least this long ends the signal. */
    static constexpr uint32_t kSignalGapUs = 150000;
    /* Longest raw signal kept, as on the Flipper Zero. */
    static constexpr std::size_t kMaxTimings = 1024;
    /* Anything shorter is a glitch, not a signal. */
    static constexpr std::size_t kMinRawTimings = 7;

    void reset();
    /* One mark or space from the receiver. Finished signals are appended to `out`. */
    void feed(bool mark, uint32_t duration_us, std::vector<IrCapture>& out);
    /*
     * Tells the collector how long the receiver has been silent. The last
     * space of a signal is never reported by a receiver, so this is what ends
     * a frame and a signal.
     */
    void idle(uint32_t silent_us, std::vector<IrCapture>& out);
    /* True between the first mark of a signal and its end. */
    bool busy() const;

private:
    void end_frame(std::vector<IrCapture>& out);
    void end_signal(std::vector<IrCapture>& out);

    std::vector<uint32_t> timings_;
    std::size_t frame_start_{0};
    /* Marks have arrived since the last frame was handed to the decoders. */
    bool frame_open_{false};
    bool decoded_{false};
    bool overflowed_{false};
    IrMessage last_message_;
};

} // namespace model
