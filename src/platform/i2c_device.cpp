/*
 * SPDX-License-Identifier: MIT
 */

#include "i2c_device.h"

#if !USE_DESKTOP && defined(__linux__)
#define ZEROSUB_HAVE_I2CDEV 1
#else
#define ZEROSUB_HAVE_I2CDEV 0
#endif

#include <algorithm>
#include <filesystem>

#if ZEROSUB_HAVE_I2CDEV
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace platform::i2c {
namespace {

/* Longest register write; the BMI270 config upload is chunked below this. */
constexpr std::size_t kMaxWriteBytes = 256;

int bus_number(const std::string& path) {
    const auto dash = path.rfind('-');
    if (dash == std::string::npos) {
        return 1 << 20;
    }
    try {
        return std::stoi(path.substr(dash + 1));
    }
    catch (...) {
        return 1 << 20;
    }
}

} // namespace

std::vector<std::string> list_buses() {
    std::vector<std::string> buses;
#if ZEROSUB_HAVE_I2CDEV
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/dev", error)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("i2c-", 0) == 0) {
            buses.push_back(entry.path().string());
        }
    }
    std::sort(buses.begin(), buses.end(), [](const std::string& left, const std::string& right) {
        return bus_number(left) < bus_number(right);
    });
#endif
    return buses;
}

Device::Device(int fd, std::string bus, uint8_t address) : fd_(fd), bus_(std::move(bus)), address_(address) {}

Device::~Device() {
#if ZEROSUB_HAVE_I2CDEV
    if (fd_ >= 0) {
        ::close(fd_);
    }
#endif
}

const std::string& Device::bus() const {
    return bus_;
}

uint8_t Device::address() const {
    return address_;
}

#if ZEROSUB_HAVE_I2CDEV

std::unique_ptr<Device> Device::open(const std::string& bus, uint8_t address, std::string& error, bool* busy) {
    if (busy) {
        *busy = false;
    }
    const int fd = ::open(bus.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        error = bus + ": " + std::strerror(errno);
        return nullptr;
    }
    /* Only used to find out whether a kernel driver owns the address; transfers go through I2C_RDWR. */
    if (::ioctl(fd, I2C_SLAVE, static_cast<unsigned long>(address)) < 0) {
        if (busy) {
            *busy = errno == EBUSY;
        }
        error = bus + ": " + std::strerror(errno);
        ::close(fd);
        return nullptr;
    }
    return std::unique_ptr<Device>(new Device(fd, bus, address));
}

bool Device::read(uint8_t reg, uint8_t* data, std::size_t length) {
    if (length == 0) {
        return true;
    }
    i2c_msg messages[2]{};
    messages[0].addr = address_;
    messages[0].flags = 0;
    messages[0].len = 1;
    messages[0].buf = &reg;
    messages[1].addr = address_;
    messages[1].flags = I2C_M_RD;
    messages[1].len = static_cast<uint16_t>(length);
    messages[1].buf = data;
    i2c_rdwr_ioctl_data transfer{messages, 2};
    return ::ioctl(fd_, I2C_RDWR, &transfer) == 2;
}

bool Device::write(uint8_t reg, const uint8_t* data, std::size_t length) {
    if (length + 1 > kMaxWriteBytes) {
        return false;
    }
    uint8_t buffer[kMaxWriteBytes];
    buffer[0] = reg;
    if (length > 0) {
        std::memcpy(buffer + 1, data, length);
    }
    i2c_msg message{};
    message.addr = address_;
    message.flags = 0;
    message.len = static_cast<uint16_t>(length + 1);
    message.buf = buffer;
    i2c_rdwr_ioctl_data transfer{&message, 1};
    return ::ioctl(fd_, I2C_RDWR, &transfer) == 1;
}

#else // !ZEROSUB_HAVE_I2CDEV

std::unique_ptr<Device> Device::open(const std::string& bus, uint8_t, std::string& error, bool* busy) {
    if (busy) {
        *busy = false;
    }
    error = bus + ": I2C is not supported on this build";
    return nullptr;
}

bool Device::read(uint8_t, uint8_t*, std::size_t) {
    return false;
}

bool Device::write(uint8_t, const uint8_t*, std::size_t) {
    return false;
}

#endif

bool Device::read_byte(uint8_t reg, uint8_t& value) {
    return read(reg, &value, 1);
}

bool Device::write_byte(uint8_t reg, uint8_t value) {
    return write(reg, &value, 1);
}

} // namespace platform::i2c
