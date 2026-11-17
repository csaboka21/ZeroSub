/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace platform {

/*
 * A continuous square wave from a UART's TX pin: 0x55 sent back to back at
 * 8N1 alternates every bit, start and stop bits included, so 250 kbaud gives
 * 125 kHz. Used as the 125 kHz reader's carrier.
 *
 * A worker thread keeps the driver's buffer full. Destroying the carrier
 * stops it and discards what is still queued, leaving TX idle (high).
 * Device builds only.
 */
class UartCarrier {
public:
    static std::unique_ptr<UartCarrier> start(const std::string& device, uint32_t baud, std::string& error);

    ~UartCarrier();
    UartCarrier(const UartCarrier&) = delete;
    UartCarrier& operator=(const UartCarrier&) = delete;

    /* The rate the driver reports after setting it; it may round. */
    uint32_t actual_baud() const;
    /* False once the worker stopped on a write error. */
    bool healthy() const;

private:
    UartCarrier() = default;
    void run();

    int fd_{-1};
    uint32_t actual_baud_{0};
    std::thread worker_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> healthy_{true};
};

} // namespace platform
