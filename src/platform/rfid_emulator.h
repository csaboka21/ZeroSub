/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gpio_line.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace platform {

/*
 * Where the 125 kHz front-end is wired. The front-end is passive: a coil tuned
 * to 125 kHz, a bridge rectifier, and a transistor that loads the rectified
 * side while the modulation line is high. The field line, optional, reads high
 * while a reader's field powers the coil.
 */
struct RfidConfig {
    std::string gpio_chip{"/dev/gpiochip0"};
    /* GPIO23, EXT pin 3. Negative disables the tool. */
    int modulation_gpio{23};
    /* GPIO22, EXT pin 5. Negative: no field detect, the key is sent all the time. */
    int field_gpio{22};
    /*
     * Reading. The carrier UART's TX pin (GPIO14, EXT pin 14) drives the coil
     * at 125 kHz and the comparator output comes back on the reader GPIO
     * (GPIO15, EXT pin 12). A negative reader GPIO or an empty UART disables
     * reading. The relay GPIO is only for the rev B front-end, which switched
     * the coil with a relay (high = reader mode); rev C needs none, so it
     * defaults to off.
     */
    int relay_gpio{-1};
    int reader_gpio{15};
    std::string carrier_uart{"/dev/serial0"};
};

/*
 * Plays one 125 kHz key over and over on the modulation line, from a thread
 * of its own: the reader sees the coil's load change and reads the bits.
 * Edges are timed against absolute deadlines on the monotonic clock, with
 * real-time priority when the process may have it, like PulseTransmitter.
 *
 * With field detect, the key is only sent while a reader is near; the field
 * line is sampled during unloaded half-bits, as loading the coil pulls it low.
 * When the field line cannot be taken (for instance GPIO22 is held by the
 * NFC reader's chip-select overlay), the key is sent all the time instead.
 *
 * Both pins' previous functions are saved when they are taken and restored
 * when the emulator is destroyed. Device builds only.
 */
class RfidEmulator {
public:
    enum class Phase {
        /* Field detect is on and no reader is near. */
        Waiting,
        Sending,
        /* The worker gave up; see Status::error. */
        Failed,
    };

    struct Status {
        Phase phase{Phase::Waiting};
        /* False when the field line could not be used. */
        bool field_detect{false};
        /* Frames sent since the emulator started. */
        uint32_t frames{0};
        /* How late the latest edge of the current or last burst was, in microseconds. */
        uint32_t worst_late_us{0};
        std::string error;
        /* Why field detect is off, when it is. */
        std::string field_note;
    };

    /*
     * Takes the pins and starts the worker. `half_bits` is one frame as load
     * levels (true = loaded), each half-bit lasting `half_bit_us`.
     */
    static std::unique_ptr<RfidEmulator> start(const RfidConfig& config,
                                               std::vector<bool> half_bits,
                                               uint32_t half_bit_us,
                                               std::string& error);

    ~RfidEmulator();
    RfidEmulator(const RfidEmulator&) = delete;
    RfidEmulator& operator=(const RfidEmulator&) = delete;

    /* Changes whenever the status changes, so a poll can skip unchanged ones. */
    uint32_t revision() const;
    Status status() const;

private:
    RfidEmulator() = default;
    void run();
    void publish(Phase phase, const std::string& error = std::string());

    std::unique_ptr<gpio::OutputLine> modulation_;
    std::unique_ptr<gpio::InputLine> field_;
    gpio::PinFunctionSnapshot modulation_pin_;
    gpio::PinFunctionSnapshot field_pin_;
    std::vector<bool> half_bits_;
    uint32_t half_bit_us_{256};
    std::thread worker_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<uint32_t> revision_{0};
    mutable std::mutex mutex_;
    Status status_;
};

} // namespace platform
