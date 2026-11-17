/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace platform::i2c {

/* The /dev/i2c-* nodes present, sorted by bus number. Empty on desktop builds. */
std::vector<std::string> list_buses();

/*
 * One chip on a Linux i2c-dev bus. Device builds only: on desktop builds
 * open() always fails with an explicit "unsupported" error.
 */
class Device {
public:
    /*
     * Opens `bus` for the chip at `address`. Fails with `busy` set when a
     * kernel driver has claimed that address.
     */
    static std::unique_ptr<Device> open(const std::string& bus, uint8_t address, std::string& error, bool* busy = nullptr);

    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    const std::string& bus() const;
    uint8_t address() const;

    /* Register read with a repeated start. */
    bool read(uint8_t reg, uint8_t* data, std::size_t length);
    bool read_byte(uint8_t reg, uint8_t& value);
    /* Register write: `reg` followed by `length` bytes in one message. */
    bool write(uint8_t reg, const uint8_t* data, std::size_t length);
    bool write_byte(uint8_t reg, uint8_t value);

private:
    Device(int fd, std::string bus, uint8_t address);

    int fd_{-1};
    std::string bus_;
    uint8_t address_{0};
};

} // namespace platform::i2c
