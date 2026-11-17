/*
 * SPDX-License-Identifier: MIT
 */

#include "pulse_transmitter.h"

#include <algorithm>
#include <utility>

#if !USE_DESKTOP && defined(__linux__)
#define ZEROSUB_HAVE_PULSE_TRANSMITTER 1
#else
#define ZEROSUB_HAVE_PULSE_TRANSMITTER 0
#endif

#if ZEROSUB_HAVE_PULSE_TRANSMITTER
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif

namespace platform {

#if ZEROSUB_HAVE_PULSE_TRANSMITTER

namespace {

/* Gaps longer than this are slept through instead of spun through. */
constexpr int64_t kSleepThresholdNs = 2000000;
/* Wake up this long before the edge and spin for the rest. */
constexpr int64_t kSpinMarginNs = 500000;
/* While sleeping, look for a stop request at least this often. */
constexpr int64_t kStopCheckNs = 20000000;

int64_t now_ns() {
    timespec time{};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return static_cast<int64_t>(time.tv_sec) * 1000000000LL + time.tv_nsec;
}

void sleep_until_ns(int64_t deadline_ns) {
    timespec time{};
    time.tv_sec = static_cast<time_t>(deadline_ns / 1000000000LL);
    time.tv_nsec = static_cast<long>(deadline_ns % 1000000000LL);
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time, nullptr);
}

} // namespace

std::unique_ptr<PulseTransmitter> PulseTransmitter::open(const std::string& chip_path,
                                                         unsigned int line_offset,
                                                         std::string& error) {
    error.clear();
    std::unique_ptr<PulseTransmitter> transmitter(new PulseTransmitter());

    /* Only take the pin over if its current function can be put back afterwards. */
    if (!transmitter->pin_state_.capture(line_offset, error)) {
        return nullptr;
    }
    transmitter->line_ = gpio::OutputLine::request(chip_path, line_offset, false, error);
    if (!transmitter->line_) {
        transmitter->pin_state_.restore();
        return nullptr;
    }
    return transmitter;
}

PulseTransmitter::~PulseTransmitter() {
    stop();
    if (line_) {
        std::string ignored;
        line_->set_value(false, ignored);
        line_.reset();
    }
    pin_state_.restore();
}

void PulseTransmitter::start(std::vector<int32_t> pulses) {
    if (worker_.joinable()) {
        return;
    }
    stop_requested_ = false;
    running_ = true;
    worker_ = std::thread(&PulseTransmitter::play, this, std::move(pulses));
}

bool PulseTransmitter::running() const {
    return running_;
}

void PulseTransmitter::stop() {
    stop_requested_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
}

PulseTransmitter::Result PulseTransmitter::result() const {
    return running_ ? Result{} : result_;
}

void PulseTransmitter::play(std::vector<int32_t> pulses) {
    /* Best effort: without the privilege the train still plays, with more jitter. */
    sched_param priority{};
    priority.sched_priority = 50;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &priority);

    Result result;
    result.completed = true;
    std::string error;
    int64_t worst_late_ns = 0;
    int64_t deadline_ns = now_ns();
    bool first_edge = true;

    for (const int32_t pulse : pulses) {
        if (pulse == 0) {
            continue;
        }
        if (stop_requested_) {
            result.completed = false;
            break;
        }

        if (!line_->set_value(pulse > 0, error)) {
            result.completed = false;
            result.error = error;
            break;
        }
        if (first_edge) {
            /* Everything is timed from the moment the first level is on the pin. */
            deadline_ns = now_ns();
            first_edge = false;
        }
        else {
            worst_late_ns = std::max(worst_late_ns, now_ns() - deadline_ns);
        }

        deadline_ns += static_cast<int64_t>(pulse > 0 ? pulse : -static_cast<int64_t>(pulse)) * 1000LL;

        /* Sleep through the bulk of a long pulse, in slices so a stop request is noticed. */
        while (!stop_requested_ && deadline_ns - now_ns() > kSleepThresholdNs) {
            const int64_t wake_ns = deadline_ns - kSpinMarginNs;
            sleep_until_ns(std::min(wake_ns, now_ns() + kStopCheckNs));
        }
        while (!stop_requested_ && now_ns() < deadline_ns) {
            /* Spin: sleeping this close to the edge would cost more accuracy than it saves. */
        }
    }

    line_->set_value(false, error);
    result.worst_late_us = static_cast<uint32_t>((worst_late_ns + 500) / 1000);
    result_ = result;
    running_ = false;
}

#else

std::unique_ptr<PulseTransmitter> PulseTransmitter::open(const std::string&,
                                                         unsigned int,
                                                         std::string& error) {
    error = "transmitting is only available on the device";
    return nullptr;
}

PulseTransmitter::~PulseTransmitter() = default;

void PulseTransmitter::start(std::vector<int32_t>) {}

bool PulseTransmitter::running() const {
    return false;
}

void PulseTransmitter::stop() {}

PulseTransmitter::Result PulseTransmitter::result() const {
    return result_;
}

void PulseTransmitter::play(std::vector<int32_t>) {}

#endif

} // namespace platform
