/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gpio_line.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace platform {

/*
 * Records the pulse train on one GPIO input, such as the CC1101 asynchronous
 * data output on GDO0.
 *
 * A worker thread reads kernel edge events, which carry kernel timestamps, and
 * turns them into signed durations in microseconds: positive for time spent
 * high, negative for time spent low. That is the layout of a Flipper RAW file.
 *
 * Pulses are only kept while the gate is open. A short history from just
 * before the gate opened is kept too, so the start of a transmission is not
 * lost to the delay in noticing it. Time skipped while the gate was closed is
 * recorded as one low period.
 *
 * Device builds only: on desktop builds open() fails with an "unsupported" error.
 */
class PulseCapture {
public:
    /*
     * Requests the line as an input with edge detection and starts the worker.
     * `pull_up` turns on the SoC's internal pull-up, for open-collector sources.
     */
    static std::unique_ptr<PulseCapture> open(const std::string& chip_path,
                                              unsigned int line_offset,
                                              std::string& error,
                                              bool pull_up = false);

    /* Stops the worker, releases the line, and restores the pin's previous function. */
    ~PulseCapture();
    PulseCapture(const PulseCapture&) = delete;
    PulseCapture& operator=(const PulseCapture&) = delete;

    void set_gate(bool open);
    /* Discards everything recorded so far. */
    void clear();
    std::size_t sample_count() const;
    /* Total edge events seen since the capture was opened. */
    uint64_t edge_count() const;
    /* Edge events the kernel dropped because they arrived faster than they were read. */
    uint64_t dropped_edges() const;
    std::vector<int32_t> samples() const;
    /* Removes and returns everything recorded so far, for consumers that process pulses as they arrive. */
    std::vector<int32_t> drain();

private:
    struct Pulse {
        int32_t duration_us;
        uint64_t end_ns;
    };

    PulseCapture() = default;

    void run();
    void handle_edge(uint64_t timestamp_ns, bool rising);
    /* Appends one duration, merging it into the last one when both have the same sign. */
    void append(int32_t duration_us);

    int fd_{-1};
    gpio::PinFunctionSnapshot pin_state_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> gate_open_{false};
    std::atomic<uint64_t> dropped_edges_{0};
    std::atomic<uint64_t> edge_count_{0};

    mutable std::mutex mutex_;
    std::vector<int32_t> samples_;

    /* Worker-thread state. */
    std::deque<Pulse> history_;
    bool have_previous_edge_{false};
    uint64_t previous_edge_ns_{0};
    bool was_gate_open_{false};
    bool have_recorded_end_{false};
    uint64_t recorded_end_ns_{0};
    bool have_sequence_{false};
    uint32_t last_sequence_{0};
};

} // namespace platform
