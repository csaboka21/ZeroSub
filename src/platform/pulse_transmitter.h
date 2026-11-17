/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gpio_line.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace platform {

/*
 * Plays a pulse train on one GPIO line with microsecond timing: the reverse of
 * PulseCapture. The line drives the radio's data input, so a high level keys
 * the carrier.
 *
 * Pulses are timed against absolute deadlines on the monotonic clock, so an
 * edge that is late does not shift the ones after it. The worker runs with
 * real-time priority when the process is allowed to ask for it, sleeps through
 * long gaps and spins for the last stretch before every edge. Linux is not a
 * real-time system, so edges can still be late; the worst case is reported.
 *
 * The pin's previous function is saved when the line is taken and restored
 * when the transmitter is destroyed. Device builds only.
 */
class PulseTransmitter {
public:
    struct Result {
        /* False when stop() cut the train short or the line could not be driven. */
        bool completed{false};
        /* How late the latest edge was, in microseconds. */
        uint32_t worst_late_us{0};
        std::string error;
    };

    static std::unique_ptr<PulseTransmitter> open(const std::string& chip_path,
                                                  unsigned int line_offset,
                                                  std::string& error);

    ~PulseTransmitter();
    PulseTransmitter(const PulseTransmitter&) = delete;
    PulseTransmitter& operator=(const PulseTransmitter&) = delete;

    /*
     * Starts playing signed microsecond durations: positive drives the line
     * high, negative low. The line is left low afterwards. Call once.
     */
    void start(std::vector<int32_t> pulses);
    bool running() const;
    /* Ends the train early and waits for the worker. Safe to call at any time. */
    void stop();
    /* Only meaningful once running() is false. */
    Result result() const;

private:
    PulseTransmitter() = default;
    void play(std::vector<int32_t> pulses);

    std::unique_ptr<gpio::OutputLine> line_;
    gpio::PinFunctionSnapshot pin_state_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    Result result_;
};

} // namespace platform
