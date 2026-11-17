/*
 * SPDX-License-Identifier: MIT
 */

#include "spi_device.h"

#include <cstddef>

#if !USE_DESKTOP && defined(__linux__)
#define ZEROSUB_HAVE_SPIDEV 1
#else
#define ZEROSUB_HAVE_SPIDEV 0
#endif

#if ZEROSUB_HAVE_SPIDEV
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace platform::spi {

namespace {

/* Covers a full ST25R3916 FIFO (512 bytes) plus its opcode; spidev allows 4096. */
constexpr std::size_t kMaxTransferBytes = 600;

#if ZEROSUB_HAVE_SPIDEV
std::string errno_text(const std::string& what) {
    return what + ": " + std::strerror(errno);
}
#endif

} // namespace

Device::Device(int fd, const DeviceConfig& config) : fd_(fd), config_(config) {}

#if ZEROSUB_HAVE_SPIDEV

std::unique_ptr<Device> Device::open(const DeviceConfig& config, std::string& error) {
    error.clear();
    if (config.path.empty()) {
        error = "no SPI device configured";
        return nullptr;
    }

    const int fd = ::open(config.path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        error = errno_text("open " + config.path);
        return nullptr;
    }

    uint8_t mode = config.mode;
    uint8_t bits = config.bits_per_word;
    uint32_t speed = config.speed_hz;
    if (::ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ::ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ::ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        error = errno_text("configure " + config.path);
        ::close(fd);
        return nullptr;
    }

    return std::unique_ptr<Device>(new Device(fd, config));
}

Device::~Device() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool Device::transfer(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx, std::string& error) {
    error.clear();
    if (tx.empty() || tx.size() > kMaxTransferBytes) {
        error = "invalid SPI transfer size";
        return false;
    }

    rx.assign(tx.size(), 0);
    spi_ioc_transfer xfer;
    std::memset(&xfer, 0, sizeof(xfer));
    xfer.tx_buf = reinterpret_cast<uintptr_t>(tx.data());
    xfer.rx_buf = reinterpret_cast<uintptr_t>(rx.data());
    xfer.len = static_cast<uint32_t>(tx.size());
    xfer.speed_hz = config_.speed_hz;
    xfer.bits_per_word = config_.bits_per_word;

    if (::ioctl(fd_, SPI_IOC_MESSAGE(1), &xfer) < 0) {
        error = errno_text("SPI transfer");
        return false;
    }
    return true;
}

bool Device::write_frames(const std::vector<std::vector<uint8_t>>& frames, std::string& error) {
    error.clear();
    constexpr std::size_t kMaxFrames = 8;
    if (frames.empty() || frames.size() > kMaxFrames) {
        error = "invalid SPI frame count";
        return false;
    }

    spi_ioc_transfer xfers[kMaxFrames];
    std::memset(xfers, 0, sizeof(xfers));
    for (std::size_t index = 0; index < frames.size(); ++index) {
        const auto& frame = frames[index];
        if (frame.empty() || frame.size() > kMaxTransferBytes) {
            error = "invalid SPI transfer size";
            return false;
        }
        xfers[index].tx_buf = reinterpret_cast<uintptr_t>(frame.data());
        xfers[index].len = static_cast<uint32_t>(frame.size());
        xfers[index].speed_hz = config_.speed_hz;
        xfers[index].bits_per_word = config_.bits_per_word;
        /* Release chip select after every frame but the last, which ends the message anyway. */
        xfers[index].cs_change = index + 1 < frames.size() ? 1 : 0;
    }

    if (::ioctl(fd_, SPI_IOC_MESSAGE(frames.size()), xfers) < 0) {
        error = errno_text("SPI transfer");
        return false;
    }
    return true;
}

#else // !ZEROSUB_HAVE_SPIDEV

std::unique_ptr<Device> Device::open(const DeviceConfig&, std::string& error) {
    error = "SPI is only available on the device";
    return nullptr;
}

Device::~Device() = default;

bool Device::transfer(const std::vector<uint8_t>&, std::vector<uint8_t>&, std::string& error) {
    (void)kMaxTransferBytes;
    error = "SPI is only available on the device";
    return false;
}

bool Device::write_frames(const std::vector<std::vector<uint8_t>>&, std::string& error) {
    error = "SPI is only available on the device";
    return false;
}

#endif

} // namespace platform::spi
