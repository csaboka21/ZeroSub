/*
 * SPDX-License-Identifier: MIT
 */

#include "uart_carrier.h"

#if !USE_DESKTOP && defined(__linux__)
#define ZEROSUB_HAVE_UART_CARRIER 1
#else
#define ZEROSUB_HAVE_UART_CARRIER 0
#endif

#if ZEROSUB_HAVE_UART_CARRIER
/* termios2 and BOTHER allow any rate; <termios.h> must not be included next to it. */
#include <asm/termbits.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace platform {

uint32_t UartCarrier::actual_baud() const {
    return actual_baud_;
}

bool UartCarrier::healthy() const {
    return healthy_;
}

#if ZEROSUB_HAVE_UART_CARRIER

namespace {

std::string errno_text(const std::string& what) {
    return what + ": " + std::strerror(errno);
}

} // namespace

std::unique_ptr<UartCarrier> UartCarrier::start(const std::string& device, uint32_t baud, std::string& error) {
    error.clear();
    if (device.empty()) {
        error = "Set [rfid] carrier_uart in zerosub.conf";
        return nullptr;
    }
    std::unique_ptr<UartCarrier> carrier(new UartCarrier());
    carrier->fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (carrier->fd_ < 0) {
        error = errno_text("open " + device);
        return nullptr;
    }

    struct termios2 settings;
    std::memset(&settings, 0, sizeof(settings));
    if (::ioctl(carrier->fd_, TCGETS2, &settings) < 0) {
        error = errno_text("read " + device + " settings");
        return nullptr;
    }
    /* Raw 8N1 at an arbitrary rate, no flow control. */
    settings.c_iflag = 0;
    settings.c_oflag = 0;
    settings.c_lflag = 0;
    settings.c_cflag &= ~(CBAUD | CSIZE | PARENB | CSTOPB | CRTSCTS);
    settings.c_cflag |= BOTHER | CS8 | CLOCAL | CREAD;
    settings.c_ispeed = baud;
    settings.c_ospeed = baud;
    if (::ioctl(carrier->fd_, TCSETS2, &settings) < 0) {
        error = errno_text("set " + device + " to " + std::to_string(baud) + " baud");
        return nullptr;
    }
    if (::ioctl(carrier->fd_, TCGETS2, &settings) == 0) {
        carrier->actual_baud_ = settings.c_ospeed;
    }

    carrier->worker_ = std::thread(&UartCarrier::run, carrier.get());
    return carrier;
}

UartCarrier::~UartCarrier() {
    stop_requested_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
    if (fd_ >= 0) {
        /* Drop what is queued so the carrier stops now, not when the buffer drains. */
        ::ioctl(fd_, TCFLSH, TCOFLUSH);
        ::close(fd_);
    }
}

void UartCarrier::run() {
    unsigned char block[512];
    std::memset(block, 0x55, sizeof(block));
    while (!stop_requested_) {
        pollfd descriptor{};
        descriptor.fd = fd_;
        descriptor.events = POLLOUT;
        /* Short timeout, so a stop request is seen quickly. */
        if (::poll(&descriptor, 1, 50) <= 0) {
            continue;
        }
        const ssize_t written = ::write(fd_, block, sizeof(block));
        if (written < 0 && errno != EAGAIN && errno != EINTR) {
            healthy_ = false;
            return;
        }
    }
}

#else

std::unique_ptr<UartCarrier> UartCarrier::start(const std::string&, uint32_t, std::string& error) {
    error = "the 125 kHz carrier is only available on the device";
    return nullptr;
}

UartCarrier::~UartCarrier() = default;

void UartCarrier::run() {}

#endif

} // namespace platform
