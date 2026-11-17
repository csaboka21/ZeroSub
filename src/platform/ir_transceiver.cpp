/*
 * SPDX-License-Identifier: MIT
 */

#include "ir_transceiver.h"

#include <chrono>
#include <cstddef>
#include <utility>

#if !USE_DESKTOP && defined(__linux__)
#define ZEROSUB_HAVE_LIRC 1
#else
#define ZEROSUB_HAVE_LIRC 0
#endif

#if ZEROSUB_HAVE_LIRC
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/lirc.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace platform {
namespace {

#if ZEROSUB_HAVE_LIRC

constexpr int kMaxLircDevices = 8;
/* The kernel ends a signal after this much silence, and says so. */
constexpr uint32_t kReceiveTimeoutUs = 150000;
/* What read() reports for the end of a signal: longer than any gap inside one. */
constexpr uint32_t kEndOfSignalUs = 1000000;

/*
 * Limits of one write to a LIRC device: the kernel takes at most 1024
 * durations adding up to less than half a second. A longer signal is handed
 * over in pieces, cut at a silence.
 */
constexpr std::size_t kMaxChunkEntries = 999;
constexpr uint64_t kMaxChunkUs = 400000;
/* A silence at least this long is always waited out here, not in the kernel. */
constexpr uint32_t kCutGapUs = 15000;

std::string errno_text(const std::string& what) {
    return what + ": " + std::strerror(errno);
}

/* Opens one LIRC device if it offers `feature`; -1 otherwise. */
int open_with_feature(const std::string& path, int flags, uint32_t feature, std::string& error) {
    const int fd = ::open(path.c_str(), flags | O_CLOEXEC);
    if (fd < 0) {
        error = errno_text("open " + path);
        return -1;
    }
    uint32_t features = 0;
    if (::ioctl(fd, LIRC_GET_FEATURES, &features) < 0) {
        error = errno_text(path + " is not a LIRC device");
        ::close(fd);
        return -1;
    }
    if ((features & feature) == 0) {
        error = path + (feature == LIRC_CAN_SEND_PULSE ? " cannot send" : " cannot receive");
        ::close(fd);
        return -1;
    }
    return fd;
}

/* The configured device, or the first /dev/lirc* that offers `feature`. */
int open_lirc(const std::string& device, int flags, uint32_t feature, std::string& path, std::string& error) {
    if (!device.empty()) {
        path = device;
        return open_with_feature(device, flags, feature, error);
    }
    for (int index = 0; index < kMaxLircDevices; ++index) {
        std::string ignored;
        const std::string candidate = "/dev/lirc" + std::to_string(index);
        const int fd = open_with_feature(candidate, flags, feature, ignored);
        if (fd >= 0) {
            path = candidate;
            return fd;
        }
    }
    error = feature == LIRC_CAN_SEND_PULSE ? "No infrared transmitter (no /dev/lirc* can send)"
                                           : "No infrared receiver (no /dev/lirc* can receive)";
    return -1;
}

#else

constexpr const char* kNoLirc = "Infrared needs the device: there is no LIRC in desktop builds";

#endif

} // namespace

#if ZEROSUB_HAVE_LIRC

std::unique_ptr<IrReceiver> IrReceiver::open(const std::string& device, std::string& error) {
    error.clear();
    std::string path;
    const int fd = open_lirc(device, O_RDONLY | O_NONBLOCK, LIRC_CAN_REC_MODE2, path, error);
    if (fd < 0) {
        return nullptr;
    }

    /* Both are the defaults on current kernels; a driver that refuses them still works. */
    uint32_t mode = LIRC_MODE_MODE2;
    (void)::ioctl(fd, LIRC_SET_REC_MODE, &mode);
    uint32_t timeout_us = kReceiveTimeoutUs;
    (void)::ioctl(fd, LIRC_SET_REC_TIMEOUT, &timeout_us);

    std::unique_ptr<IrReceiver> receiver(new IrReceiver());
    receiver->fd_ = fd;
    receiver->path_ = path;
    return receiver;
}

IrReceiver::~IrReceiver() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool IrReceiver::read(std::vector<IrSample>& samples, std::string& error) {
    error.clear();
    uint32_t buffer[256];
    for (;;) {
        const ssize_t count = ::read(fd_, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            error = errno_text("read " + path_);
            return false;
        }
        if (count == 0) {
            return true;
        }

        const std::size_t values = static_cast<std::size_t>(count) / sizeof(uint32_t);
        for (std::size_t index = 0; index < values; ++index) {
            const uint32_t value = buffer[index];
            switch (LIRC_MODE2(value)) {
                case LIRC_MODE2_PULSE:
                    samples.push_back({true, LIRC_VALUE(value)});
                    break;
                case LIRC_MODE2_SPACE:
                    samples.push_back({false, LIRC_VALUE(value)});
                    break;
                case LIRC_MODE2_TIMEOUT:
                case LIRC_MODE2_OVERFLOW:
                    /* Quiet for long enough, or edges were lost: either way this signal is over. */
                    samples.push_back({false, kEndOfSignalUs});
                    break;
                default:
                    break;
            }
        }
        if (static_cast<std::size_t>(count) < sizeof(buffer)) {
            return true;
        }
    }
}

std::unique_ptr<IrTransmitter> IrTransmitter::open(const std::string& device, std::string& error) {
    error.clear();
    std::string path;
    const int fd = open_lirc(device, O_RDWR, LIRC_CAN_SEND_PULSE, path, error);
    if (fd < 0) {
        return nullptr;
    }

    uint32_t mode = LIRC_MODE_PULSE;
    (void)::ioctl(fd, LIRC_SET_SEND_MODE, &mode);

    std::unique_ptr<IrTransmitter> transmitter(new IrTransmitter());
    transmitter->fd_ = fd;
    transmitter->path_ = path;
    return transmitter;
}

IrTransmitter::~IrTransmitter() {
    stop();
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void IrTransmitter::play(std::vector<uint32_t> timings, uint32_t carrier_hz, float duty_cycle) {
    Result result;

    /* A driver without a settable carrier or duty cycle sends on its own; that is not an error. */
    uint32_t carrier = carrier_hz;
    (void)::ioctl(fd_, LIRC_SET_SEND_CARRIER, &carrier);
    uint32_t duty_percent = static_cast<uint32_t>(duty_cycle * 100.0F + 0.5F);
    if (duty_percent < 1 || duty_percent > 99) {
        duty_percent = 33;
    }
    (void)::ioctl(fd_, LIRC_SET_SEND_DUTY_CYCLE, &duty_percent);

    /* A signal ends on a mark. */
    if (timings.size() % 2 == 0 && !timings.empty()) {
        timings.pop_back();
    }

    std::vector<uint32_t> chunk;
    std::size_t index = 0;
    bool failed = false;
    while (index < timings.size() && !stop_requested_.load()) {
        chunk.clear();
        chunk.push_back(timings[index]);
        uint64_t chunk_us = timings[index];
        ++index;

        uint32_t gap_us = 0;
        while (index + 1 < timings.size()) {
            const uint32_t space = timings[index];
            const uint32_t mark = timings[index + 1];
            if (space >= kCutGapUs || chunk_us + space + mark > kMaxChunkUs ||
                chunk.size() + 2 > kMaxChunkEntries) {
                gap_us = space;
                ++index;
                break;
            }
            chunk.push_back(space);
            chunk.push_back(mark);
            chunk_us += static_cast<uint64_t>(space) + mark;
            index += 2;
        }

        /* Returns once the piece has been sent. */
        const std::size_t bytes = chunk.size() * sizeof(uint32_t);
        ssize_t written = -1;
        do {
            written = ::write(fd_, chunk.data(), bytes);
        } while (written < 0 && errno == EINTR);
        if (written != static_cast<ssize_t>(bytes)) {
            result.error = written < 0 ? errno_text("write " + path_) : "short write to " + path_;
            failed = true;
            break;
        }

        /* Wait out the silence in short steps, so stop() does not have to. */
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(gap_us);
        while (gap_us > 0 && !stop_requested_.load()) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                break;
            }
            const auto step = std::chrono::milliseconds(20);
            std::this_thread::sleep_for(deadline - now < step ? deadline - now : step);
        }
    }

    result.completed = !failed && index >= timings.size();
    result_ = std::move(result);
    running_.store(false);
}

#else

std::unique_ptr<IrReceiver> IrReceiver::open(const std::string&, std::string& error) {
    error = kNoLirc;
    return nullptr;
}

IrReceiver::~IrReceiver() = default;

bool IrReceiver::read(std::vector<IrSample>&, std::string& error) {
    error = kNoLirc;
    return false;
}

std::unique_ptr<IrTransmitter> IrTransmitter::open(const std::string&, std::string& error) {
    error = kNoLirc;
    return nullptr;
}

IrTransmitter::~IrTransmitter() {
    stop();
}

void IrTransmitter::play(std::vector<uint32_t>, uint32_t, float) {
    result_ = Result{};
    running_.store(false);
}

#endif

const std::string& IrReceiver::path() const {
    return path_;
}

void IrTransmitter::start(std::vector<uint32_t> timings, uint32_t carrier_hz, float duty_cycle) {
    if (worker_.joinable()) {
        return;
    }
    stop_requested_.store(false);
    running_.store(true);
    worker_ = std::thread([this, timings = std::move(timings), carrier_hz, duty_cycle]() mutable {
        play(std::move(timings), carrier_hz, duty_cycle);
    });
}

bool IrTransmitter::running() const {
    return running_.load();
}

void IrTransmitter::stop() {
    stop_requested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
}

IrTransmitter::Result IrTransmitter::result() const {
    return result_;
}

const std::string& IrTransmitter::path() const {
    return path_;
}

} // namespace platform
