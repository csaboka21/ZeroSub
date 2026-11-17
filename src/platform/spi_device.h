/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace platform::spi {

struct DeviceConfig {
    std::string path;
    uint32_t speed_hz{1000000};
    uint8_t mode{0};
    uint8_t bits_per_word{8};
};

/*
 * Owning handle for a Linux spidev node. Device builds only: on desktop builds
 * open() always fails with an explicit "unsupported" error.
 */
class Device {
public:
    static std::unique_ptr<Device> open(const DeviceConfig& config, std::string& error);

    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    /* Full-duplex transfer under a single chip-select assertion. */
    bool transfer(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx, std::string& error);
    /*
     * Several write-only frames in one system call, chip select released
     * between them. Saves a round trip through the kernel per frame where
     * the answer time matters.
     */
    bool write_frames(const std::vector<std::vector<uint8_t>>& frames, std::string& error);

private:
    Device(int fd, const DeviceConfig& config);

    int fd_{-1};
    DeviceConfig config_;
};

} // namespace platform::spi
