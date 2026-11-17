/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace platform {

struct ImuConfig {
    /* i2c-dev node the BMI270 is on, e.g. /dev/i2c-1. Empty searches every /dev/i2c-* bus. */
    std::string i2c_device;
    /* False disables the IMU tool. */
    bool enabled{true};
};

/* One set of readings, in the sensors' own axes. */
struct ImuReading {
    bool motion_valid{false};
    std::array<float, 3> accel_g{};
    std::array<float, 3> gyro_dps{};
    bool temperature_valid{false};
    float temperature_c{0.0F};
    bool field_valid{false};
    std::array<float, 3> field_ut{};
};

/*
 * Finds the BMI270 and the BMM150 (on the bus itself or behind the BMI270's
 * auxiliary interface), sets them up and reads them on a thread of its own.
 * Where a kernel driver already owns them, reads the IIO sysfs files instead.
 * Stopping is the destructor.
 */
class ImuWorker {
public:
    enum class Phase {
        /* Searching the buses and uploading the BMI270 config. */
        Starting,
        Running,
        /* Nothing found, or setup failed; the thread has ended. */
        Failed,
    };

    struct Status {
        Phase phase{Phase::Starting};
        /* Where each chip was found, or why not. */
        std::string bmi270;
        std::string bmm150;
        /* One line for the page while starting or after a failure. */
        std::string text;
        ImuReading reading;
        /* Readings taken since the start; tells a stalled bus from a quiet one. */
        uint32_t samples{0};
    };

    static std::unique_ptr<ImuWorker> start(const ImuConfig& config);

    ~ImuWorker();
    ImuWorker(const ImuWorker&) = delete;
    ImuWorker& operator=(const ImuWorker&) = delete;

    /* Changes whenever the status does. */
    uint32_t revision() const;
    Status status() const;

private:
    explicit ImuWorker(const ImuConfig& config);

    void run();
    /* Each returns false when it found nothing to read; `why` then says so. */
    bool run_i2c(std::string& why);
    bool run_iio(std::string& why);
    void publish(const Status& status);
    bool cancelled() const;
    bool sleep_for(unsigned int duration_ms) const;

    const ImuConfig config_;
    std::atomic<bool> cancel_{false};
    std::atomic<uint32_t> revision_{0};
    mutable std::mutex mutex_;
    Status status_;
    std::thread thread_;
};

} // namespace platform
