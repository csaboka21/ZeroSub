/*
 * SPDX-License-Identifier: MIT
 */

#include "pulse_capture.h"

#if !USE_DESKTOP && defined(__linux__)
#define ZEROSUB_HAVE_PULSE_CAPTURE 1
#else
#define ZEROSUB_HAVE_PULSE_CAPTURE 0
#endif

#if ZEROSUB_HAVE_PULSE_CAPTURE
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <linux/gpio.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace platform {

#if ZEROSUB_HAVE_PULSE_CAPTURE

namespace {

/* Pulses kept from just before the gate opened. */
constexpr std::size_t kHistoryPulses = 256;
constexpr int kPollTimeoutMs = 100;
constexpr std::size_t kEventsPerRead = 256;
/* Kernel-side queue of unread edge events; the kernel caps this if it is too large. */
constexpr uint32_t kKernelEventBuffer = 8192;

int32_t to_microseconds(uint64_t duration_ns) {
    const uint64_t microseconds = (duration_ns + 500) / 1000;
    const auto limit = static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
    if (microseconds == 0) {
        return 1;
    }
    return static_cast<int32_t>(microseconds > limit ? limit : microseconds);
}

} // namespace

std::unique_ptr<PulseCapture> PulseCapture::open(const std::string& chip_path,
                                                 unsigned int line_offset,
                                                 std::string& error,
                                                 bool pull_up) {
    error.clear();
    std::unique_ptr<PulseCapture> capture(new PulseCapture());

    /* Only take the pin over if its current function can be put back afterwards. */
    if (!capture->pin_state_.capture(line_offset, error)) {
        return nullptr;
    }

    const int chip_fd = ::open(chip_path.c_str(), O_RDWR | O_CLOEXEC);
    if (chip_fd < 0) {
        error = "open " + chip_path + ": " + std::strerror(errno);
        capture->pin_state_.restore();
        return nullptr;
    }

    gpio_v2_line_request request;
    std::memset(&request, 0, sizeof(request));
    request.offsets[0] = line_offset;
    request.num_lines = 1;
    std::snprintf(request.consumer, sizeof(request.consumer), "zerosub-rx");
    request.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING |
                           GPIO_V2_LINE_FLAG_EDGE_FALLING |
                           (pull_up ? GPIO_V2_LINE_FLAG_BIAS_PULL_UP : GPIO_V2_LINE_FLAG_BIAS_DISABLED);
    request.event_buffer_size = kKernelEventBuffer;

    const int result = ::ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &request);
    if (result < 0) {
        error = "request GPIO" + std::to_string(line_offset) + ": " + std::strerror(errno);
    }
    ::close(chip_fd);
    if (result < 0) {
        capture->pin_state_.restore();
        return nullptr;
    }

    capture->fd_ = request.fd;
    capture->running_ = true;
    capture->worker_ = std::thread(&PulseCapture::run, capture.get());
    return capture;
}

PulseCapture::~PulseCapture() {
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
    if (fd_ >= 0) {
        ::close(fd_);
    }
    pin_state_.restore();
}

void PulseCapture::run() {
    gpio_v2_line_event events[kEventsPerRead];
    pollfd descriptor{};
    descriptor.fd = fd_;
    descriptor.events = POLLIN;

    while (running_) {
        const int ready = ::poll(&descriptor, 1, kPollTimeoutMs);
        if (ready <= 0 || (descriptor.revents & POLLIN) == 0) {
            continue;
        }

        const ssize_t bytes = ::read(fd_, events, sizeof(events));
        if (bytes <= 0) {
            continue;
        }

        const auto count = static_cast<std::size_t>(bytes) / sizeof(events[0]);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& event = events[index];
            /* A jump in the per-line sequence number means the kernel queue overflowed. */
            if (have_sequence_ && event.line_seqno > last_sequence_ + 1) {
                dropped_edges_ += event.line_seqno - last_sequence_ - 1;
            }
            last_sequence_ = event.line_seqno;
            have_sequence_ = true;

            handle_edge(event.timestamp_ns, event.id == GPIO_V2_LINE_EVENT_RISING_EDGE);
        }
        edge_count_ += count;
    }
}

void PulseCapture::handle_edge(uint64_t timestamp_ns, bool rising) {
    if (!have_previous_edge_ || timestamp_ns <= previous_edge_ns_) {
        previous_edge_ns_ = timestamp_ns;
        have_previous_edge_ = true;
        return;
    }

    /* A rising edge ends a low period, a falling edge ends a high period. */
    const int32_t magnitude = to_microseconds(timestamp_ns - previous_edge_ns_);
    const Pulse pulse{rising ? -magnitude : magnitude, timestamp_ns};
    previous_edge_ns_ = timestamp_ns;

    const bool gate_open = gate_open_;
    if (!gate_open) {
        was_gate_open_ = false;
        history_.push_back(pulse);
        if (history_.size() > kHistoryPulses) {
            history_.pop_front();
        }
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!was_gate_open_) {
        was_gate_open_ = true;
        if (!history_.empty()) {
            /* Account for the time that was skipped while the gate was closed. */
            const auto& first = history_.front();
            const uint64_t first_length_ns =
                static_cast<uint64_t>(first.duration_us < 0 ? -static_cast<int64_t>(first.duration_us)
                                                             : first.duration_us) *
                1000U;
            const uint64_t first_start_ns =
                first.end_ns > first_length_ns ? first.end_ns - first_length_ns : 0;
            if (have_recorded_end_ && !samples_.empty() && first_start_ns > recorded_end_ns_) {
                append(-to_microseconds(first_start_ns - recorded_end_ns_));
            }
            for (const auto& earlier : history_) {
                append(earlier.duration_us);
            }
            history_.clear();
        }
        else if (have_recorded_end_ && !samples_.empty() &&
                 timestamp_ns > recorded_end_ns_ + static_cast<uint64_t>(magnitude) * 1000U) {
            append(-to_microseconds(timestamp_ns - static_cast<uint64_t>(magnitude) * 1000U -
                                    recorded_end_ns_));
        }
    }

    append(pulse.duration_us);
    recorded_end_ns_ = timestamp_ns;
    have_recorded_end_ = true;
}

void PulseCapture::append(int32_t duration_us) {
    if (!samples_.empty() && (samples_.back() < 0) == (duration_us < 0)) {
        const int64_t merged = static_cast<int64_t>(samples_.back()) + duration_us;
        const int64_t limit = std::numeric_limits<int32_t>::max();
        samples_.back() = static_cast<int32_t>(merged > limit ? limit : (merged < -limit ? -limit : merged));
        return;
    }
    samples_.push_back(duration_us);
}

#else // !ZEROSUB_HAVE_PULSE_CAPTURE

std::unique_ptr<PulseCapture> PulseCapture::open(const std::string&, unsigned int, std::string& error, bool) {
    error = "Signal capture is only available on the device";
    return nullptr;
}

PulseCapture::~PulseCapture() = default;

void PulseCapture::run() {}

void PulseCapture::handle_edge(uint64_t, bool) {}

void PulseCapture::append(int32_t) {}

#endif

void PulseCapture::set_gate(bool open) {
    gate_open_ = open;
}

void PulseCapture::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    samples_.clear();
    have_recorded_end_ = false;
    dropped_edges_ = 0;
}

std::size_t PulseCapture::sample_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_.size();
}

uint64_t PulseCapture::dropped_edges() const {
    return dropped_edges_;
}

std::vector<int32_t> PulseCapture::samples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_;
}

std::vector<int32_t> PulseCapture::drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int32_t> taken;
    taken.swap(samples_);
    return taken;
}

uint64_t PulseCapture::edge_count() const {
    return edge_count_;
}

} // namespace platform
