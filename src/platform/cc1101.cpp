/*
 * SPDX-License-Identifier: MIT
 */

#include "cc1101.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>
#include <vector>

namespace platform {
namespace {

/* SPI header bits. */
constexpr uint8_t kRead = 0x80;
constexpr uint8_t kBurst = 0x40;

/* Configuration registers. */
constexpr uint8_t kRegIocfg2 = 0x00;
constexpr uint8_t kRegIocfg0 = 0x02;
constexpr uint8_t kRegPktctrl0 = 0x08;
constexpr uint8_t kRegFsctrl1 = 0x0B;
constexpr uint8_t kRegFreq2 = 0x0D;
constexpr uint8_t kRegFreq1 = 0x0E;
constexpr uint8_t kRegFreq0 = 0x0F;
constexpr uint8_t kRegMdmcfg4 = 0x10;
constexpr uint8_t kRegMdmcfg3 = 0x11;
constexpr uint8_t kRegMdmcfg2 = 0x12;
constexpr uint8_t kRegDeviatn = 0x15;
constexpr uint8_t kRegMcsm0 = 0x18;
constexpr uint8_t kRegFoccfg = 0x19;
constexpr uint8_t kRegAgcctrl2 = 0x1B;
constexpr uint8_t kRegAgcctrl1 = 0x1C;
constexpr uint8_t kRegAgcctrl0 = 0x1D;
constexpr uint8_t kRegFrend1 = 0x21;
constexpr uint8_t kRegFrend0 = 0x22;
/* Output power table: eight bytes, written in one burst. */
constexpr uint8_t kRegPatable = 0x3E;

/* Status registers (read with the burst bit set). */
constexpr uint8_t kStatusPartnum = 0x30;
constexpr uint8_t kStatusVersion = 0x31;
constexpr uint8_t kStatusRssi = 0x34;
constexpr uint8_t kStatusMarcstate = 0x35;
constexpr uint8_t kMarcstateTransmit = 0x13;

/* Command strobes. */
constexpr uint8_t kStrobeReset = 0x30;
constexpr uint8_t kStrobeReceive = 0x34;
constexpr uint8_t kStrobeTransmit = 0x35;
constexpr uint8_t kStrobeIdle = 0x36;

constexpr uint8_t kGdoHighImpedance = 0x2E;
constexpr uint8_t kGdoAsyncSerialData = 0x0D;
/* GDO2 feeds the Cap's second antenna-switch input: a constant low or high level. */
constexpr uint8_t kGdo2ConstantLow = 0x2F;
constexpr uint8_t kGdo2ConstantHigh = 0x6F;
constexpr int kIdentifyAttempts = 5;
/* About +10 dBm at 433 MHz and +12 dBm at 868 MHz, the level the Flipper Zero presets use. */
constexpr uint8_t kTransmitPower = 0xC0;
constexpr int kTransmitStartAttempts = 20;
constexpr int32_t kRssiOffsetDb = 74;

/*
 * Modem settings per preset, for a 26 MHz crystal. The four presets mirror the
 * ones the Flipper Zero Sub-GHz app offers.
 */
struct PresetRegisters {
    const char* name;
    const char* sub_file_name;
    uint8_t mdmcfg4; /* receive bandwidth + data-rate exponent */
    uint8_t mdmcfg3; /* data-rate mantissa */
    uint8_t mdmcfg2; /* modulation format, sync mode */
    uint8_t deviatn; /* FSK deviation */
    uint8_t foccfg;
    uint8_t frend1;
};

constexpr PresetRegisters kPresets[] = {
    /* OOK, 270 kHz bandwidth */
    {"AM270", "FuriHalSubGhzPresetOok270Async", 0x67, 0x32, 0x30, 0x47, 0x18, 0xB6},
    /* OOK, 650 kHz bandwidth */
    {"AM650", "FuriHalSubGhzPresetOok650Async", 0x17, 0x32, 0x30, 0x47, 0x18, 0xB6},
    /* 2-FSK, 2.38 kHz deviation, 270 kHz bandwidth */
    {"FM238", "FuriHalSubGhzPreset2FSKDev238Async", 0x67, 0x83, 0x04, 0x04, 0x16, 0x56},
    /* 2-FSK, 47.6 kHz deviation, 270 kHz bandwidth */
    {"FM476", "FuriHalSubGhzPreset2FSKDev476Async", 0x67, 0x83, 0x04, 0x47, 0x16, 0x56},
};

const PresetRegisters& preset_registers(Cc1101Preset preset) {
    return kPresets[static_cast<std::size_t>(preset)];
}

} // namespace

const char* cc1101_preset_name(Cc1101Preset preset) {
    return preset_registers(preset).name;
}

const char* cc1101_preset_sub_file_name(Cc1101Preset preset) {
    return preset_registers(preset).sub_file_name;
}

Cc1101::Cc1101(std::unique_ptr<spi::Device> device, const Cc1101Config& config)
    : device_(std::move(device)), config_(config) {}

Cc1101::~Cc1101() {
    std::string ignored;
    set_data_output(false, ignored);
    idle(ignored);
    if (rf_switch_) {
        rf_switch_->set_value(false, ignored);
        rf_switch_.reset();
    }
    rf_switch_pin_state_.restore();
}

std::unique_ptr<Cc1101> Cc1101::open(const Cc1101Config& config, std::string& error) {
    error.clear();
    if (config.crystal_hz == 0) {
        error = "invalid crystal frequency";
        return nullptr;
    }

    spi::DeviceConfig spi_config;
    spi_config.path = config.spi_device;
    spi_config.speed_hz = config.spi_speed_hz;
    spi_config.mode = 0;
    auto device = spi::Device::open(spi_config, error);
    if (!device) {
        return nullptr;
    }

    std::unique_ptr<Cc1101> radio(new Cc1101(std::move(device), config));
    if (!radio->reset_and_identify(error) || !radio->apply_preset(Cc1101Preset::Am650, error)) {
        return nullptr;
    }
    radio->claim_antenna_switch();
    return radio;
}

const Cc1101ChipInfo& Cc1101::chip_info() const {
    return chip_info_;
}

bool Cc1101::antenna_switch_active() const {
    return rf_switch_ != nullptr;
}

void Cc1101::claim_antenna_switch() {
    if (config_.rf_switch_line < 0) {
        return;
    }

    /* Only take the pin over if its current function can be put back afterwards. */
    const auto line = static_cast<unsigned int>(config_.rf_switch_line);
    std::string ignored;
    if (!rf_switch_pin_state_.capture(line, ignored)) {
        return;
    }
    rf_switch_ = gpio::OutputLine::request(config_.gpio_chip, line, false, ignored);
    if (!rf_switch_) {
        rf_switch_pin_state_.restore();
    }
}

bool Cc1101::select_antenna(uint32_t frequency_hz, std::string& error) {
    /* Band split and levels follow M5Stack's Cap CC1101 reference driver. */
    const bool low_band = frequency_hz < 374000000U;
    const bool mid_band = !low_band && frequency_hz < 650500000U;
    if (rf_switch_ && !rf_switch_->set_value(!low_band, error)) {
        return false;
    }
    return write_register(kRegIocfg2, mid_band ? kGdo2ConstantLow : kGdo2ConstantHigh, error);
}

bool Cc1101::strobe(uint8_t command, std::string& error) {
    std::vector<uint8_t> rx;
    return device_->transfer({command}, rx, error);
}

bool Cc1101::write_register(uint8_t address, uint8_t value, std::string& error) {
    std::vector<uint8_t> rx;
    return device_->transfer({address, value}, rx, error);
}

bool Cc1101::read_status_register(uint8_t address, uint8_t& value, std::string& error) {
    std::vector<uint8_t> rx;
    if (!device_->transfer({static_cast<uint8_t>(address | kRead | kBurst), 0x00}, rx, error)) {
        return false;
    }
    value = rx[1];
    return true;
}

bool Cc1101::reset_and_identify(std::string& error) {
    if (!strobe(kStrobeReset, error)) {
        return false;
    }

    /* A missing or unpowered chip reads back as all zeros or all ones. */
    bool identified = false;
    for (int attempt = 0; attempt < kIdentifyAttempts && !identified; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (!read_status_register(kStatusPartnum, chip_info_.part_number, error) ||
            !read_status_register(kStatusVersion, chip_info_.version, error)) {
            return false;
        }
        identified = chip_info_.version != 0x00 && chip_info_.version != 0xFF;
    }

    if (!identified) {
        char text[64];
        std::snprintf(text,
                      sizeof(text),
                      "no CC1101 found (part 0x%02X, version 0x%02X)",
                      chip_info_.part_number,
                      chip_info_.version);
        error = text;
        return false;
    }
    return true;
}

bool Cc1101::apply_preset(Cc1101Preset preset, std::string& error) {
    const auto& registers = preset_registers(preset);
    preset_ = preset;
    /* Modem registers may only be changed while the chip is idle. */
    return strobe(kStrobeIdle, error) &&
           /* GDO0 only drives its host line while a capture has asked for the data output. */
           write_register(kRegIocfg0, data_output_ ? kGdoAsyncSerialData : kGdoHighImpedance, error) &&
           /* Asynchronous serial mode: the RX FIFO is unused, so it cannot overflow. */
           write_register(kRegPktctrl0, 0x32, error) &&
           write_register(kRegFsctrl1, 0x06, error) &&
           write_register(kRegMdmcfg4, registers.mdmcfg4, error) &&
           write_register(kRegMdmcfg3, registers.mdmcfg3, error) &&
           write_register(kRegMdmcfg2, registers.mdmcfg2, error) &&
           write_register(kRegDeviatn, registers.deviatn, error) &&
           /* Calibrate automatically when leaving IDLE. */
           write_register(kRegMcsm0, 0x18, error) &&
           write_register(kRegFoccfg, registers.foccfg, error) &&
           write_register(kRegAgcctrl2, 0x07, error) &&
           write_register(kRegAgcctrl1, 0x00, error) &&
           write_register(kRegAgcctrl0, 0x91, error) &&
           write_register(kRegFrend1, registers.frend1, error);
}

bool Cc1101::set_data_output(bool enabled, std::string& error) {
    data_output_ = enabled;
    return write_register(kRegIocfg0, enabled ? kGdoAsyncSerialData : kGdoHighImpedance, error);
}

bool Cc1101::tune(uint32_t frequency_hz, std::string& error) {
    const bool in_band = (frequency_hz >= 300000000U && frequency_hz <= 348000000U) ||
                         (frequency_hz >= 387000000U && frequency_hz <= 464000000U) ||
                         (frequency_hz >= 779000000U && frequency_hz <= 928000000U);
    if (!in_band) {
        error = "frequency outside the CC1101 bands";
        return false;
    }

    const uint64_t word = (static_cast<uint64_t>(frequency_hz) << 16) / config_.crystal_hz;
    return strobe(kStrobeIdle, error) &&
           select_antenna(frequency_hz, error) &&
           write_register(kRegFreq2, static_cast<uint8_t>((word >> 16) & 0xFF), error) &&
           write_register(kRegFreq1, static_cast<uint8_t>((word >> 8) & 0xFF), error) &&
           write_register(kRegFreq0, static_cast<uint8_t>(word & 0xFF), error);
}

bool Cc1101::start_receive(uint32_t frequency_hz, std::string& error) {
    return tune(frequency_hz, error) && strobe(kStrobeReceive, error);
}

bool Cc1101::start_transmit(uint32_t frequency_hz, std::string& error) {
    /*
     * In asynchronous serial mode the chip takes its transmit data from GDO0,
     * so the pin must be an input here before the host starts driving the line.
     */
    if (!set_data_output(false, error) || !tune(frequency_hz, error)) {
        return false;
    }

    /*
     * OOK switches between two power table entries: off for a 0 and full power
     * for a 1. FSK keeps the carrier on and uses the first entry only.
     */
    const bool ook = preset_ == Cc1101Preset::Am270 || preset_ == Cc1101Preset::Am650;
    std::vector<uint8_t> power_table(9, 0x00);
    power_table[0] = kRegPatable | kBurst;
    power_table[ook ? 2 : 1] = kTransmitPower;
    std::vector<uint8_t> rx;
    if (!write_register(kRegFrend0, ook ? 0x11 : 0x10, error) ||
        !device_->transfer(power_table, rx, error) || !strobe(kStrobeTransmit, error)) {
        return false;
    }

    /* The chip calibrates before the carrier is ready; wait until it says it is transmitting. */
    for (int attempt = 0; attempt < kTransmitStartAttempts; ++attempt) {
        uint8_t state = 0;
        if (!read_status_register(kStatusMarcstate, state, error)) {
            return false;
        }
        if ((state & 0x1F) == kMarcstateTransmit) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::string ignored;
    strobe(kStrobeIdle, ignored);
    error = "the CC1101 did not enter transmit";
    return false;
}

bool Cc1101::read_rssi_dbm(int32_t& rssi_dbm, std::string& error) {
    /* The register can change mid-read; accept the first value seen twice in a row. */
    uint8_t previous = 0;
    if (!read_status_register(kStatusRssi, previous, error)) {
        return false;
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint8_t current = 0;
        if (!read_status_register(kStatusRssi, current, error)) {
            return false;
        }
        if (current == previous) {
            break;
        }
        previous = current;
    }

    const int32_t raw = previous >= 128 ? static_cast<int32_t>(previous) - 256
                                        : static_cast<int32_t>(previous);
    rssi_dbm = raw / 2 - kRssiOffsetDb;
    return true;
}

bool Cc1101::idle(std::string& error) {
    return strobe(kStrobeIdle, error);
}

} // namespace platform
