/*
 * SPDX-License-Identifier: MIT
 */

#include "rfid_reader.h"

#include <chrono>

namespace platform {
namespace {

/* EM4100 at RF/64: 64 field cycles of 8 us per bit, so a half-bit is 256 us. */
constexpr uint32_t kHalfBitUs = 256;
constexpr uint32_t kCarrierBaud = 250000;
/* How long the relay contacts get to settle before the carrier starts. */
constexpr auto kRelaySettle = std::chrono::milliseconds(20);
/* How often the worker drains the captured pulses. */
constexpr auto kPollPeriod = std::chrono::milliseconds(20);

} // namespace

std::unique_ptr<RfidReader> RfidReader::start(const RfidConfig& config, std::string& error) {
    error.clear();
    if (config.reader_gpio < 0 || config.carrier_uart.empty()) {
        error = "Reading is off: set reader_gpio and carrier_uart in [rfid]";
        return nullptr;
    }

    std::unique_ptr<RfidReader> reader(new RfidReader());
    /* Only the rev B front-end has a mode relay; rev C drives the coil directly. */
    if (config.relay_gpio >= 0) {
        const auto relay_gpio = static_cast<unsigned int>(config.relay_gpio);
        if (!reader->relay_pin_.capture(relay_gpio, error)) {
            return nullptr;
        }
        reader->relay_ = gpio::OutputLine::request(config.gpio_chip, relay_gpio, true, error);
        if (!reader->relay_) {
            reader->relay_pin_.restore();
            return nullptr;
        }
        std::this_thread::sleep_for(kRelaySettle);
    }

    reader->carrier_ = UartCarrier::start(config.carrier_uart, kCarrierBaud, error);
    if (!reader->carrier_) {
        error = "Carrier: " + error;
        return nullptr;
    }
    reader->status_.carrier_baud = reader->carrier_->actual_baud();

    reader->capture_ =
        PulseCapture::open(config.gpio_chip, static_cast<unsigned int>(config.reader_gpio), error, true);
    if (!reader->capture_) {
        error = "Reader input: " + error;
        return nullptr;
    }
    reader->capture_->set_gate(true);

    reader->worker_ = std::thread(&RfidReader::run, reader.get());
    return reader;
}

RfidReader::~RfidReader() {
    stop_requested_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
    capture_.reset();
    carrier_.reset();
    if (relay_) {
        /* Back to tag mode before the pin is handed back; R13 keeps it there afterwards. */
        std::string ignored;
        relay_->set_value(false, ignored);
        relay_.reset();
    }
    relay_pin_.restore();
}

uint32_t RfidReader::revision() const {
    return revision_;
}

RfidReader::Status RfidReader::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void RfidReader::clear() {
    clear_requested_ = true;
}

void RfidReader::run() {
    model::Em4100Decoder decoder(kHalfBitUs);
    bool have_candidate = false;
    std::array<uint8_t, model::kEm4100DataSize> candidate{};
    uint64_t published_edges = 0;

    while (!stop_requested_) {
        std::this_thread::sleep_for(kPollPeriod);

        bool changed = false;
        if (clear_requested_.exchange(false)) {
            decoder.reset();
            have_candidate = false;
            std::lock_guard<std::mutex> lock(mutex_);
            status_.have_id = false;
            status_.frames = 0;
            changed = true;
        }

        for (const int32_t pulse : capture_->drain()) {
            decoder.feed(pulse);
            std::array<uint8_t, model::kEm4100DataSize> id{};
            if (!decoder.take(id)) {
                continue;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            ++status_.frames;
            /* One frame could be a fluke; the same ID twice in a row is a read. */
            if (have_candidate && candidate == id) {
                status_.have_id = true;
                status_.id = id;
            }
            candidate = id;
            have_candidate = true;
            changed = true;
        }

        const uint64_t edges = capture_->edge_count();
        if (!carrier_->healthy()) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (status_.error.empty()) {
                status_.error = "Carrier stopped: UART write failed";
                changed = true;
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.edges = edges;
        }
        /* Edge counts change all the time; publish them a few times a second, not on every poll. */
        if (changed || edges - published_edges >= 50) {
            published_edges = edges;
            ++revision_;
        }
    }
}

} // namespace platform
