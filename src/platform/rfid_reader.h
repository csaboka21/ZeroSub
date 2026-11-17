/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gpio_line.h"
#include "pulse_capture.h"
#include "rfid/em4100.h"
#include "rfid_emulator.h"
#include "uart_carrier.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace platform {

/*
 * Reads EM4100 tags with the 125 kHz front-end in reader mode.
 *
 * Starting it starts the 125 kHz carrier on the UART, which the driver feeds
 * into the series-resonant coil, and times the comparator's edges on the
 * reader GPIO with the internal pull-up on. A worker feeds the pulses to an
 * EM4100 decoder. An ID counts as read once the same one has been decoded
 * twice in a row. With a rev B front-end, the mode relay is switched on first.
 *
 * Destroying the reader stops the carrier (TX goes back to idle, which is
 * what emulation needs), switches a relay back off and restores the pins.
 * The EXT 5 V rail must already be on.
 * Device builds only.
 */
class RfidReader {
public:
    struct Status {
        /* Edges seen on the reader GPIO since the start. */
        uint64_t edges{0};
        /* Decoded frames, confirmed or not. */
        uint32_t frames{0};
        /* True once `id` has been decoded twice in a row. */
        bool have_id{false};
        std::array<uint8_t, model::kEm4100DataSize> id{};
        uint32_t carrier_baud{0};
        std::string error;
    };

    static std::unique_ptr<RfidReader> start(const RfidConfig& config, std::string& error);

    ~RfidReader();
    RfidReader(const RfidReader&) = delete;
    RfidReader& operator=(const RfidReader&) = delete;

    uint32_t revision() const;
    Status status() const;
    /* Forgets the ID read so far and waits for the next one. */
    void clear();

private:
    RfidReader() = default;
    void run();

    gpio::PinFunctionSnapshot relay_pin_;
    std::unique_ptr<gpio::OutputLine> relay_;
    std::unique_ptr<UartCarrier> carrier_;
    std::unique_ptr<PulseCapture> capture_;
    std::thread worker_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> clear_requested_{false};
    std::atomic<uint32_t> revision_{0};
    mutable std::mutex mutex_;
    Status status_;
};

} // namespace platform
