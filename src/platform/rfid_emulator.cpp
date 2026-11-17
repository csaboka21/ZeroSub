/*
 * SPDX-License-Identifier: MIT
 */

#include "rfid_emulator.h"

#include <algorithm>
#include <utility>

#if !USE_DESKTOP && defined(__linux__)
#define ZEROSUB_HAVE_RFID_EMULATOR 1
#else
#define ZEROSUB_HAVE_RFID_EMULATOR 0
#endif

#if ZEROSUB_HAVE_RFID_EMULATOR
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif

namespace platform {

uint32_t RfidEmulator::revision() const {
    return revision_;
}

RfidEmulator::Status RfidEmulator::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void RfidEmulator::publish(Phase phase, const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.phase = phase;
        if (!error.empty()) {
            status_.error = error;
        }
    }
    ++revision_;
}

#if ZEROSUB_HAVE_RFID_EMULATOR

namespace {

/* Sleep until this long before an edge, then spin: sleeping closer costs accuracy. */
constexpr int64_t kSpinMarginNs = 100000;
/* While no reader is near, look at the field line this often. */
constexpr int64_t kIdlePollNs = 5000000;
/* The field must be seen on this many polls in a row before sending starts. */
constexpr int kFieldOnPolls = 2;
/* Sending stops after this many frames in a row without the field. */
constexpr int kFieldOffFrames = 8;
/* Sample the field line this long before the end of an unloaded half-bit, so the node has recovered. */
constexpr int64_t kSampleBeforeEndNs = 40000;
/* Publish the frame count this often while sending, in frames (about half a second at RF/64). */
constexpr uint32_t kFramesPerUpdate = 16;

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

/*
 * The half-bit to sample the field in: preferably the second of two unloaded
 * halves in a row, which gives the rectified node the longest time to recover.
 */
std::size_t field_sample_index(const std::vector<bool>& halves) {
    for (std::size_t index = 1; index < halves.size(); ++index) {
        if (!halves[index] && !halves[index - 1]) {
            return index;
        }
    }
    for (std::size_t index = 0; index < halves.size(); ++index) {
        if (!halves[index]) {
            return index;
        }
    }
    return halves.size();
}

} // namespace

std::unique_ptr<RfidEmulator> RfidEmulator::start(const RfidConfig& config,
                                                  std::vector<bool> half_bits,
                                                  uint32_t half_bit_us,
                                                  std::string& error) {
    error.clear();
    if (config.modulation_gpio < 0) {
        error = "Set [rfid] modulation_gpio in zerosub.conf";
        return nullptr;
    }
    if (half_bits.empty() || half_bit_us == 0) {
        error = "nothing to send";
        return nullptr;
    }

    std::unique_ptr<RfidEmulator> emulator(new RfidEmulator());
    const auto modulation_gpio = static_cast<unsigned int>(config.modulation_gpio);
    /* Only take the pin over if its current function can be put back afterwards. */
    if (!emulator->modulation_pin_.capture(modulation_gpio, error)) {
        return nullptr;
    }
    emulator->modulation_ = gpio::OutputLine::request(config.gpio_chip, modulation_gpio, false, error);
    if (!emulator->modulation_) {
        emulator->modulation_pin_.restore();
        return nullptr;
    }

    /* Field detect is a nicety: without it the key is simply sent all the time. */
    if (config.field_gpio >= 0 && config.field_gpio != config.modulation_gpio) {
        const auto field_gpio = static_cast<unsigned int>(config.field_gpio);
        std::string field_error;
        if (emulator->field_pin_.capture(field_gpio, field_error)) {
            emulator->field_ = gpio::InputLine::request(config.gpio_chip, field_gpio, field_error);
            if (!emulator->field_) {
                emulator->field_pin_.restore();
            }
        }
        if (!emulator->field_) {
            emulator->status_.field_note = "No field detect: " + field_error;
        }
    }
    else {
        emulator->status_.field_note = "No field detect configured";
    }
    emulator->status_.field_detect = emulator->field_ != nullptr;
    emulator->status_.phase = emulator->field_ ? Phase::Waiting : Phase::Sending;

    emulator->half_bits_ = std::move(half_bits);
    emulator->half_bit_us_ = half_bit_us;
    emulator->worker_ = std::thread(&RfidEmulator::run, emulator.get());
    return emulator;
}

RfidEmulator::~RfidEmulator() {
    stop_requested_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
    if (modulation_) {
        std::string ignored;
        modulation_->set_value(false, ignored);
        modulation_.reset();
    }
    field_.reset();
    field_pin_.restore();
    modulation_pin_.restore();
}

void RfidEmulator::run() {
    /* Best effort: without the privilege the key is still sent, with more jitter. */
    sched_param priority{};
    priority.sched_priority = 50;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &priority);

    const int64_t half_ns = static_cast<int64_t>(half_bit_us_) * 1000LL;
    const std::size_t sample_index = field_sample_index(half_bits_);
    std::string error;
    bool sending = !field_;
    int field_polls = 0;
    int frames_without_field = 0;
    uint32_t frames = 0;

    while (!stop_requested_) {
        if (!sending) {
            /* Waiting for a reader: the coil is left unloaded so the field line can be read. */
            if (!modulation_->set_value(false, error)) {
                publish(Phase::Failed, error);
                return;
            }
            field_polls = field_->active() ? field_polls + 1 : 0;
            if (field_polls < kFieldOnPolls) {
                sleep_until_ns(now_ns() + kIdlePollNs);
                continue;
            }
            sending = true;
            frames_without_field = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                status_.worst_late_us = 0;
            }
            publish(Phase::Sending);
        }

        /* One burst: frames back to back, timed from the first edge. */
        int64_t deadline_ns = now_ns();
        int64_t worst_late_ns = 0;
        while (sending && !stop_requested_) {
            bool field_seen = false;
            for (std::size_t index = 0; index < half_bits_.size() && !stop_requested_; ++index) {
                const int64_t edge_ns = deadline_ns;
                if (!modulation_->set_value(half_bits_[index], error)) {
                    publish(Phase::Failed, error);
                    return;
                }
                worst_late_ns = std::max(worst_late_ns, now_ns() - edge_ns);
                deadline_ns += half_ns;

                if (deadline_ns - now_ns() > kSpinMarginNs + 50000) {
                    sleep_until_ns(deadline_ns - kSpinMarginNs);
                }
                if (field_ && index == sample_index) {
                    while (now_ns() < deadline_ns - kSampleBeforeEndNs) {
                    }
                    field_seen = field_->active();
                }
                while (now_ns() < deadline_ns) {
                    /* Spin to the edge. */
                }
            }
            ++frames;

            if (field_) {
                frames_without_field = field_seen ? 0 : frames_without_field + 1;
                if (frames_without_field >= kFieldOffFrames) {
                    sending = false;
                    field_polls = 0;
                }
            }

            if (!sending || frames % kFramesPerUpdate == 0) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    status_.frames = frames;
                    status_.worst_late_us = static_cast<uint32_t>((worst_late_ns + 500) / 1000);
                }
                publish(sending ? Phase::Sending : Phase::Waiting);
            }
        }
    }

    modulation_->set_value(false, error);
}

#else

std::unique_ptr<RfidEmulator> RfidEmulator::start(const RfidConfig&,
                                                  std::vector<bool>,
                                                  uint32_t,
                                                  std::string& error) {
    error = "125 kHz emulation is only available on the device";
    return nullptr;
}

RfidEmulator::~RfidEmulator() = default;

void RfidEmulator::run() {}

#endif

} // namespace platform
