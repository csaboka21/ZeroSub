/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace platform {

/* One mark (carrier seen) or space (silence) from the receiver. */
struct IrSample {
    bool mark{false};
    uint32_t duration_us{0};
};

/*
 * Infrared receiver on a Linux LIRC device (/dev/lirc*), read in "mode2": the
 * kernel timestamps every edge of the demodulated signal and hands out the
 * durations. Nothing is decoded here. Device builds only; on the desktop
 * open() fails with a message saying so.
 */
class IrReceiver {
public:
    /*
     * Opens `device`, or the first LIRC device that can receive when `device`
     * is empty.
     */
    static std::unique_ptr<IrReceiver> open(const std::string& device, std::string& error);

    ~IrReceiver();
    IrReceiver(const IrReceiver&) = delete;
    IrReceiver& operator=(const IrReceiver&) = delete;

    /*
     * Appends everything received since the last call; never waits. The end of
     * a signal, which the kernel reports once the line has been quiet for a
     * while, comes out as one very long space.
     */
    bool read(std::vector<IrSample>& samples, std::string& error);

    const std::string& path() const;

private:
    IrReceiver() = default;

    int fd_{-1};
    std::string path_;
};

/*
 * Infrared transmitter on a Linux LIRC device. The kernel driver generates the
 * carrier and times the marks and spaces; a worker thread hands it the signal,
 * because that call only returns once the signal has been sent.
 */
class IrTransmitter {
public:
    struct Result {
        /* False when stop() cut the signal short or the driver refused it. */
        bool completed{false};
        std::string error;
    };

    /* Opens `device`, or the first LIRC device that can send when `device` is empty. */
    static std::unique_ptr<IrTransmitter> open(const std::string& device, std::string& error);

    ~IrTransmitter();
    IrTransmitter(const IrTransmitter&) = delete;
    IrTransmitter& operator=(const IrTransmitter&) = delete;

    /*
     * Starts sending microsecond durations that begin with a mark and
     * alternate, on the given carrier. Call once.
     */
    void start(std::vector<uint32_t> timings, uint32_t carrier_hz, float duty_cycle);
    bool running() const;
    /* Ends the signal at the next gap and waits for the worker. Safe to call at any time. */
    void stop();
    /* Only meaningful once running() is false. */
    Result result() const;

    const std::string& path() const;

private:
    IrTransmitter() = default;
    void play(std::vector<uint32_t> timings, uint32_t carrier_hz, float duty_cycle);

    int fd_{-1};
    std::string path_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    Result result_;
};

} // namespace platform
