/*
 * SPDX-License-Identifier: MIT
 *
 * The register values for the Cap's antenna and the reader set-up follow
 * M5Stack's M5Unit-NFC library (MIT, Copyright (c) 2025 M5Stack Technology
 * CO LTD) and the CardputerZero Cap-CC1101-NFC app derived from it. The
 * listener follows amiibo_sim (MIT, Copyright (c) 2026 amiibo_sim
 * contributors).
 */

#include "st25r3916.h"

#include "nfc/iso15693.h"

#include <algorithm>
#include <cstdio>
#include <thread>

namespace platform {
namespace {

/* Register space A. */
constexpr uint8_t kRegIoConfiguration1 = 0x00;
constexpr uint8_t kRegIoConfiguration2 = 0x01;
constexpr uint8_t kRegOperationControl = 0x02;
constexpr uint8_t kRegMode = 0x03;
constexpr uint8_t kRegBitrate = 0x04;
constexpr uint8_t kRegIso14443a = 0x05;
constexpr uint8_t kRegPassiveTarget = 0x08;
constexpr uint8_t kRegAuxiliary = 0x0A;
constexpr uint8_t kRegReceiver1 = 0x0B;
constexpr uint8_t kRegMaskReceiveTimer = 0x0F;
constexpr uint8_t kRegNoResponseTimer1 = 0x10;
constexpr uint8_t kRegTimerControl = 0x12;
constexpr uint8_t kRegInterruptMask = 0x16;
constexpr uint8_t kRegInterrupt = 0x1A;
constexpr uint8_t kRegFifoStatus1 = 0x1E;
constexpr uint8_t kRegPassiveTargetDisplay = 0x21;
constexpr uint8_t kRegTransmitBytes1 = 0x22;
constexpr uint8_t kRegBitrateDetection = 0x24;
constexpr uint8_t kRegAntennaTuning1 = 0x26;
constexpr uint8_t kRegAntennaTuning2 = 0x27;
constexpr uint8_t kRegTxDriver = 0x28;
constexpr uint8_t kRegPassiveTargetModulation = 0x29;
constexpr uint8_t kRegFieldOnThreshold = 0x2A;
constexpr uint8_t kRegFieldOffThreshold = 0x2B;
constexpr uint8_t kRegAdConverterOutput = 0x25;
constexpr uint8_t kRegRegulatorVoltageControl = 0x2C;
constexpr uint8_t kRegAuxiliaryDisplay = 0x31;
constexpr uint8_t kRegIdentity = 0x3F;

/* Register space B. */
constexpr uint8_t kRegStreamMode = 0x09;
constexpr uint8_t kRegBEmdSuppression = 0x05;
constexpr uint8_t kRegBCorrelator1 = 0x0C;
constexpr uint8_t kRegBCorrelator2 = 0x0D;
constexpr uint8_t kRegBFieldOnGuardTimer = 0x15;
constexpr uint8_t kRegBResistiveAm = 0x2A;
constexpr uint8_t kRegBOvershoot1 = 0x30;
constexpr uint8_t kRegBOvershoot2 = 0x31;
constexpr uint8_t kRegBUndershoot1 = 0x32;
constexpr uint8_t kRegBUndershoot2 = 0x33;

/* Direct commands. */
constexpr uint8_t kCmdSetDefault = 0xC1;
constexpr uint8_t kCmdStopAll = 0xC2;
constexpr uint8_t kCmdTransmitWithCrc = 0xC4;
constexpr uint8_t kCmdTransmitWithoutCrc = 0xC5;
constexpr uint8_t kCmdTransmitWupa = 0xC7;
constexpr uint8_t kCmdInitialFieldOn = 0xC8;
constexpr uint8_t kCmdGoToSense = 0xCD;
constexpr uint8_t kCmdGoToSleep = 0xCE;
constexpr uint8_t kCmdUnmaskReceiveData = 0xD1;
constexpr uint8_t kCmdResetRxGain = 0xD5;
constexpr uint8_t kCmdAdjustRegulators = 0xD6;
constexpr uint8_t kCmdClearFifo = 0xDB;
constexpr uint8_t kCmdMeasurePowerSupply = 0xDF;
constexpr uint8_t kCmdSpaceBAccess = 0xFB;
constexpr uint8_t kCmdTestAccess = 0xFC;

/* SPI operation codes. */
constexpr uint8_t kOpReadRegister = 0x40;
constexpr uint8_t kOpLoadFifo = 0x80;
constexpr uint8_t kOpReadFifo = 0x9F;
constexpr uint8_t kOpLoadPtMemoryA = 0xA0;

/* IO configuration 2. */
constexpr uint8_t kIoSupply3V = 0x80;

/* Operation control. */
constexpr uint8_t kOpEnable = 0x80;
constexpr uint8_t kOpRxEnable = 0x40;
constexpr uint8_t kOpTxEnable = 0x08;
constexpr uint8_t kOpWakeUp = 0x04;
constexpr uint8_t kOpFieldDetectorMask = 0x03;
constexpr uint8_t kOpFieldDetectorCollisionAvoidance = 0x01;
constexpr uint8_t kOpFieldDetectorAutomatic = 0x03;

/* Mode definition. */
constexpr uint8_t kModeInitiatorNfcA = 0x08;
constexpr uint8_t kModeTargetNfcA = 0x88;

/* ISO 14443A settings. */
constexpr uint8_t kIsoNoTxParity = 0x80;
constexpr uint8_t kIsoNoRxParity = 0x40;
constexpr uint8_t kIsoNfcF0 = 0x20;
constexpr uint8_t kIsoAnticollision = 0x01;

/* Passive target definition: each bit disables one automatic response. */
constexpr uint8_t kPtDisableAp2p = 0x08;
constexpr uint8_t kPtDisableNfcF = 0x04;
constexpr uint8_t kPtDisableNfcA = 0x01;

/* Auxiliary definition. */
constexpr uint8_t kAuxNoCrcRx = 0x80;
constexpr uint8_t kAuxUidSizeMask = 0x30;
constexpr uint8_t kAuxUid7 = 0x10;
constexpr uint8_t kAuxDisableCorrelator = 0x04;

/* Timer and EMV control. */
constexpr uint8_t kTimerMrtStep512 = 0x08;
constexpr uint8_t kTimerNrtEmv = 0x02;
constexpr uint8_t kTimerNrtStep4096 = 0x01;

/* Auxiliary display. */
constexpr uint8_t kAuxDisplayExternalField = 0x40;
constexpr uint8_t kAuxDisplayOscillatorOk = 0x10;

/* Interrupts, as one word: main, timer and NFC, error and wake-up, passive target. */
constexpr uint32_t kIrqOscillator = 0x80000000U;
constexpr uint32_t kIrqRxStart = 0x20000000U;
constexpr uint32_t kIrqRxEnd = 0x10000000U;
constexpr uint32_t kIrqTxEnd = 0x08000000U;
constexpr uint32_t kIrqCollision = 0x04000000U;
constexpr uint32_t kIrqNoResponse = 0x00400000U;
constexpr uint32_t kIrqFieldOn = 0x00100000U;
constexpr uint32_t kIrqFieldOff = 0x00080000U;
constexpr uint32_t kIrqFieldCollision = 0x00040000U;
constexpr uint32_t kIrqFieldGuardDone = 0x00020000U;
constexpr uint32_t kIrqBitrateDetected = 0x00010000U;
constexpr uint32_t kIrqCrcError = 0x00008000U;
constexpr uint32_t kIrqParityError = 0x00004000U;
constexpr uint32_t kIrqSoftFramingError = 0x00002000U;
constexpr uint32_t kIrqHardFramingError = 0x00001000U;
constexpr uint32_t kIrqPassiveTargetRxEnd = 0x00000010U;
constexpr uint32_t kIrqActiveFromHalt = 0x00000002U;
constexpr uint32_t kIrqActive = 0x00000001U;

constexpr uint32_t kIrqRxErrors =
    kIrqCrcError | kIrqParityError | kIrqSoftFramingError | kIrqHardFramingError;

/* Passive target display: states of the chip's own anticollision. */

constexpr uint8_t kIdentityTypeMask = 0xF8;
constexpr uint8_t kIdentityType = 0x28;
constexpr std::size_t kFifoDepth = 512;
/* A session the reader walked away from is dropped after this long without traffic. */

void sleep_ms(unsigned int duration_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
}

/* ISO 14443-3A CRC; 0 over a whole frame whose CRC is right. */
uint16_t crc_a(const uint8_t* bytes, std::size_t size) {
    uint16_t crc = 0x6363;
    for (std::size_t index = 0; index < size; ++index) {
        uint8_t value = static_cast<uint8_t>(bytes[index] ^ (crc & 0xFF));
        value = static_cast<uint8_t>(value ^ (value << 4));
        crc = static_cast<uint16_t>((crc >> 8) ^ (static_cast<uint16_t>(value) << 8) ^
                                    (static_cast<uint16_t>(value) << 3) ^ (value >> 4));
    }
    return crc;
}

/* No-response timer value in steps of 64 carrier periods (4.72 us). */
uint16_t no_response_ticks(unsigned int timeout_ms) {
    const uint64_t ticks = (static_cast<uint64_t>(std::max(timeout_ms, 1U)) * 13560U + 63U) / 64U;
    return static_cast<uint16_t>(std::min<uint64_t>(ticks, 0xFFFFU));
}

} // namespace

St25r3916::St25r3916(std::unique_ptr<spi::Device> device, std::unique_ptr<gpio::EdgeInput> irq)
    : device_(std::move(device)), irq_(std::move(irq)) {}

std::unique_ptr<St25r3916> St25r3916::open(const St25r3916Config& config, std::string& error) {
    error.clear();

    spi::DeviceConfig spi_config;
    spi_config.path = config.spi_device;
    spi_config.speed_hz = config.spi_speed_hz;
    spi_config.mode = 1;
    auto device = spi::Device::open(spi_config, error);
    if (!device) {
        error = "NFC: " + error;
        return nullptr;
    }

    if (config.irq_line < 0) {
        error = "NFC: no interrupt line configured";
        return nullptr;
    }
    auto irq = gpio::EdgeInput::request(config.gpio_chip, static_cast<unsigned int>(config.irq_line), error);
    if (!irq) {
        error = "NFC interrupt line: " + error;
        return nullptr;
    }

    std::unique_ptr<St25r3916> chip(new St25r3916(std::move(device), std::move(irq)));
    if (!chip->initialize(error)) {
        return nullptr;
    }
    return chip;
}

St25r3916::~St25r3916() {
    listen_state_ = ListenState::Stopped;
    command(kCmdStopAll);
    modify_register(kRegOperationControl, 0, kOpTxEnable | kOpRxEnable);
    write_interrupt_mask(0xFFFFFFFFU);
}

uint8_t St25r3916::identity() const {
    return identity_;
}

unsigned int St25r3916::supply_mv() const {
    return supply_mv_;
}

bool St25r3916::failed() const {
    return failed_;
}

const std::string& St25r3916::io_error() const {
    return io_error_;
}

/* ---- SPI access ------------------------------------------------------------ */

std::vector<uint8_t> St25r3916::xfer(const std::vector<uint8_t>& tx) {
    std::vector<uint8_t> rx(tx.size(), 0);
    if (failed_) {
        return rx;
    }
    std::string error;
    if (!device_->transfer(tx, rx, error)) {
        failed_ = true;
        io_error_ = error;
        rx.assign(tx.size(), 0);
    }
    return rx;
}

void St25r3916::write_frames(const std::vector<std::vector<uint8_t>>& frames) {
    if (failed_) {
        return;
    }
    std::string error;
    if (!device_->write_frames(frames, error)) {
        failed_ = true;
        io_error_ = error;
    }
}

uint8_t St25r3916::read_register(uint8_t reg) {
    return xfer({static_cast<uint8_t>(kOpReadRegister | (reg & 0x3F)), 0})[1];
}

void St25r3916::write_register(uint8_t reg, uint8_t value) {
    xfer({static_cast<uint8_t>(reg & 0x3F), value});
}

void St25r3916::write_registers(uint8_t reg, const uint8_t* values, std::size_t size) {
    std::vector<uint8_t> tx(size + 1);
    tx[0] = static_cast<uint8_t>(reg & 0x3F);
    std::copy_n(values, size, tx.begin() + 1);
    xfer(tx);
}

uint8_t St25r3916::read_register_b(uint8_t reg) {
    return xfer({kCmdSpaceBAccess, static_cast<uint8_t>(kOpReadRegister | (reg & 0x3F)), 0})[2];
}

void St25r3916::write_register_b(uint8_t reg, uint8_t value) {
    xfer({kCmdSpaceBAccess, static_cast<uint8_t>(reg & 0x3F), value});
}

void St25r3916::modify_register(uint8_t reg, uint8_t set_mask, uint8_t clear_mask) {
    const uint8_t current = read_register(reg);
    const uint8_t updated = static_cast<uint8_t>((current & ~clear_mask) | set_mask);
    if (updated != current) {
        write_register(reg, updated);
    }
}

void St25r3916::command(uint8_t code) {
    xfer({code});
}

void St25r3916::write_interrupt_mask(uint32_t mask) {
    const uint8_t bytes[4] = {static_cast<uint8_t>(mask >> 24),
                              static_cast<uint8_t>(mask >> 16),
                              static_cast<uint8_t>(mask >> 8),
                              static_cast<uint8_t>(mask)};
    write_registers(kRegInterruptMask, bytes, sizeof(bytes));
    interrupt_mask_ = mask;
}

void St25r3916::unmask_interrupts(uint32_t bits) {
    if ((interrupt_mask_ & bits) != 0) {
        write_interrupt_mask(interrupt_mask_ & ~bits);
    }
}

void St25r3916::mask_interrupts(uint32_t bits) {
    if ((interrupt_mask_ & bits) != bits) {
        write_interrupt_mask(interrupt_mask_ | bits);
    }
}

/* ---- Interrupts ------------------------------------------------------------ */

void St25r3916::collect_interrupts() {
    /* The four interrupt registers are read in one go; reading clears them. */
    const auto rx = xfer({static_cast<uint8_t>(kOpReadRegister | kRegInterrupt), 0, 0, 0, 0});
    pending_ |= (static_cast<uint32_t>(rx[1]) << 24) | (static_cast<uint32_t>(rx[2]) << 16) |
                (static_cast<uint32_t>(rx[3]) << 8) | rx[4];
}

void St25r3916::clear_interrupts() {
    for (int pass = 0; pass < 4; ++pass) {
        collect_interrupts();
        if (!irq_->active()) {
            break;
        }
    }
    pending_ = 0;
}

uint32_t St25r3916::take_interrupts(uint32_t mask) {
    if (irq_->active()) {
        collect_interrupts();
    }
    const uint32_t taken = pending_ & mask;
    pending_ &= ~taken;
    return taken;
}

uint32_t St25r3916::wait_interrupts(uint32_t mask, unsigned int timeout_us) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(timeout_us);
    for (;;) {
        const uint32_t taken = take_interrupts(mask);
        if (taken != 0 || failed_) {
            return taken;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return 0;
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::microseconds>(deadline - now).count();
        irq_->wait(static_cast<unsigned int>(std::min<long long>(remaining, 2000)));
        /* Read the chip even without an edge, in case one was missed. */
        collect_interrupts();
    }
}

/* ---- Set-up ---------------------------------------------------------------- */

bool St25r3916::initialize(std::string& error) {
    /* Give the chip time after power-up before the first access. */
    sleep_ms(50);
    for (int attempt = 0; attempt < 5; ++attempt) {
        identity_ = read_register(kRegIdentity);
        if ((identity_ & kIdentityTypeMask) == kIdentityType) {
            break;
        }
        sleep_ms(20);
    }
    if (failed_) {
        error = "NFC: " + io_error_;
        return false;
    }
    if ((identity_ & kIdentityTypeMask) != kIdentityType) {
        char text[96];
        std::snprintf(text,
                      sizeof(text),
                      "No ST25R3916 found (identity 0x%02X); is the Cap attached?",
                      static_cast<unsigned>(identity_));
        error = text;
        return false;
    }

    /* Stop whatever an earlier run left going, then return to the power-up state. */
    command(kCmdStopAll);
    modify_register(kRegOperationControl, 0, kOpTxEnable | kOpRxEnable);
    sleep_ms(2);
    command(kCmdSetDefault);
    /* ST erratum: keeps the overheat protection from triggering too early. */
    xfer({kCmdTestAccess, 0x04, 0x10});

    /* No clock output; 5 V supply, antenna tuning DAC on, MISO pulled down while idle. */
    const uint8_t io_configuration[2] = {0x07, 0x3C};
    write_registers(kRegIoConfiguration1, io_configuration, sizeof(io_configuration));
    write_register(kRegTxDriver, 0xD0);
    write_register_b(kRegBResistiveAm, 0x80);
    write_register_b(kRegBResistiveAm, 0x00);
    write_register(kRegFieldOnThreshold, 0x13);
    write_register(kRegFieldOffThreshold, 0x02);
    modify_register(kRegPassiveTarget, 0x50, 0xF0);
    write_register(kRegPassiveTargetModulation, 0x5F);
    write_register_b(kRegBEmdSuppression, 0x40);
    write_register(kRegAntennaTuning1, 0x82);
    write_register(kRegAntennaTuning2, 0x82);
    command(kCmdClearFifo);

    write_interrupt_mask(0xFFFFFFFFU);
    clear_interrupts();
    if (!enable_oscillator()) {
        error = failed_ ? "NFC: " + io_error_ : "NFC: the ST25R3916 oscillator did not start";
        return false;
    }
    write_interrupt_mask(0);

    /*
     * The supply range bit must match what the board actually feeds the
     * chip: set for 5 V on a 3.3 V supply, the regulators and the driver
     * stage run out of headroom, which a reader barely notices but which
     * weakens the load modulation of an emulated tag. The chip measures its
     * own supply (23.4 mV per step) and the bit follows the result.
     */
    write_register(kRegRegulatorVoltageControl, 0x00);
    command(kCmdMeasurePowerSupply);
    sleep_ms(1);
    supply_mv_ = static_cast<unsigned int>(read_register(kRegAdConverterOutput)) * 234U / 10U;
    if (supply_mv_ >= 2400 && supply_mv_ < 4000) {
        modify_register(kRegIoConfiguration2, kIoSupply3V, 0);
    }
    else if (supply_mv_ >= 4000 && supply_mv_ <= 5800) {
        modify_register(kRegIoConfiguration2, 0, kIoSupply3V);
    }

    command(kCmdAdjustRegulators);
    sleep_ms(5);
    clear_interrupts();

    if (failed_) {
        error = "NFC: " + io_error_;
        return false;
    }
    return true;
}

bool St25r3916::enable_oscillator() {
    if ((read_register(kRegOperationControl) & kOpEnable) == 0) {
        unmask_interrupts(kIrqOscillator);
        clear_interrupts();
        modify_register(kRegOperationControl, kOpEnable, 0);
        wait_interrupts(kIrqOscillator, 50000);
    }
    /* The interrupt can be missed on a warm start; the status bit is what counts. */
    for (int attempt = 0; attempt < 20; ++attempt) {
        if ((read_register(kRegAuxiliaryDisplay) & kAuxDisplayOscillatorOk) != 0) {
            return !failed_;
        }
        sleep_ms(1);
    }
    return false;
}

/* ---- Reader ---------------------------------------------------------------- */

bool St25r3916::field_on(std::string& error) {
    error.clear();
    listen_state_ = ListenState::Stopped;

    command(kCmdStopAll);
    modify_register(kRegOperationControl, 0, kOpWakeUp);
    if (!enable_oscillator()) {
        error = failed_ ? "NFC: " + io_error_ : "NFC: the ST25R3916 oscillator did not start";
        return false;
    }
    if (field_mode_ != FieldMode::A) {
        restore_nfca_registers();
        field_mode_ = FieldMode::A;
    }

    /* NFC-A reader at 106 kbit/s, with the receiver settings M5Stack uses for this antenna. */
    write_register(kRegMode, kModeInitiatorNfcA);
    write_register(kRegBitrate, 0x00);
    write_register(kRegIso14443a, 0x00);
    modify_register(kRegTimerControl, 0, kTimerNrtEmv | kTimerNrtStep4096);
    modify_register(kRegAuxiliary, 0, kAuxDisableCorrelator);
    write_register_b(kRegBOvershoot1, 0x40);
    write_register_b(kRegBOvershoot2, 0x03);
    write_register_b(kRegBUndershoot1, 0x40);
    write_register_b(kRegBUndershoot2, 0x03);
    write_register_b(kRegBCorrelator1, 0x47);
    write_register_b(kRegBCorrelator2, 0x00);
    const uint8_t receiver[4] = {0x08, 0x2D, 0xD8, 0x22};
    write_registers(kRegReceiver1, receiver, sizeof(receiver));
    command(kCmdResetRxGain);
    write_interrupt_mask(0);
    return start_field(error);
}

/* The registers NFC-B and NFC-V write besides those NFC-A sets up anyway. */
constexpr uint8_t kSavedA[7] = {kRegIoConfiguration1, 0x06 /* ISO 14443B settings */, kRegStreamMode, kRegAuxiliary,
                                kRegMaskReceiveTimer, kRegTimerControl, kRegTxDriver};
constexpr uint8_t kSavedB[9] = {kRegBEmdSuppression, 0x06, 0x0B, kRegBCorrelator1, kRegBCorrelator2,
                                0x0F, 0x28, 0x29, kRegBResistiveAm};

void St25r3916::save_nfca_registers() {
    for (std::size_t index = 0; index < nfca_saved_a_.size(); ++index) {
        nfca_saved_a_[index] = read_register(kSavedA[index]);
    }
    for (std::size_t index = 0; index < nfca_saved_b_.size(); ++index) {
        nfca_saved_b_[index] = read_register_b(kSavedB[index]);
    }
    nfca_saved_ = true;
}

void St25r3916::restore_nfca_registers() {
    if (!nfca_saved_) {
        return;
    }
    for (std::size_t index = 0; index < nfca_saved_a_.size(); ++index) {
        write_register(kSavedA[index], nfca_saved_a_[index]);
    }
    for (std::size_t index = 0; index < nfca_saved_b_.size(); ++index) {
        write_register_b(kSavedB[index], nfca_saved_b_[index]);
    }
}

bool St25r3916::field_on_nfcv(std::string& error) {
    error.clear();
    listen_state_ = ListenState::Stopped;

    command(kCmdStopAll);
    modify_register(kRegOperationControl, 0, kOpWakeUp);
    if (!enable_oscillator()) {
        error = failed_ ? "NFC: " + io_error_ : "NFC: the ST25R3916 oscillator did not start";
        return false;
    }
    if (field_mode_ == FieldMode::A) {
        save_nfca_registers();
    }
    field_mode_ = FieldMode::V;

    /*
     * The values M5Stack's library uses for NFC-V: subcarrier stream mode
     * (the framing is coded by the host), 424 kHz subcarrier, its ASK depth, and
     * receiver filters for the slow subcarrier.
     */
    const uint8_t receiver[4] = {0x13, 0x2D, 0x00, 0x00};
    write_registers(kRegReceiver1, receiver, sizeof(receiver));
    write_register(kRegTxDriver, 0x70);
    write_register(kRegIoConfiguration1, 0x07);
    write_register(kRegStreamMode, 0x38);
    write_register(kRegAuxiliary, 0x02);
    write_register_b(kRegBEmdSuppression, 0x40);
    write_register_b(0x06, 0x14); /* subcarrier start timer */
    write_register_b(0x0B, 0x0C); /* P2P receiver */
    write_register_b(kRegBCorrelator1, 0x13);
    write_register_b(kRegBCorrelator2, 0x01);
    write_register_b(0x0F, 0x00); /* squelch timer */
    write_register_b(0x28, 0x10); /* auxiliary modulation */
    write_register_b(0x29, 0x7C); /* TX driver timing */
    write_register_b(kRegBResistiveAm, 0x80);
    write_register(kRegMode, 0x70);
    modify_register(kRegTimerControl, 0, kTimerNrtEmv | kTimerNrtStep4096);
    command(kCmdResetRxGain);
    write_interrupt_mask(0);
    return start_field(error);
}

bool St25r3916::field_on_nfcb(std::string& error) {
    error.clear();
    listen_state_ = ListenState::Stopped;

    command(kCmdStopAll);
    modify_register(kRegOperationControl, 0, kOpWakeUp);
    if (!enable_oscillator()) {
        error = failed_ ? "NFC: " + io_error_ : "NFC: the ST25R3916 oscillator did not start";
        return false;
    }
    if (field_mode_ == FieldMode::A) {
        save_nfca_registers();
    }
    field_mode_ = FieldMode::B;

    /* The values M5Stack's library uses for an NFC-B reader: ISO 14443B with AM modulation, 106 kbit/s. */
    write_register(kRegMode, 0x14);
    write_register(kRegBitrate, 0x00);
    write_register(0x06, 0x00); /* ISO 14443B settings: standard SOF / EOF and guard times */
    write_register(kRegStreamMode, 0x38);
    write_register(kRegAuxiliary, 0x00);
    const uint8_t receiver[4] = {0x04, 0x3D, 0x00, 0x00};
    write_registers(kRegReceiver1, receiver, sizeof(receiver));
    write_register(kRegMaskReceiveTimer, 0x07);
    /* No-response timer counted in 64/fc steps, as exchange() expects. */
    write_register(kRegTimerControl, 0x20);
    write_register(kRegTxDriver, 0x70);
    write_register(kRegIoConfiguration1, 0x07);
    write_register_b(kRegBEmdSuppression, 0xC4);
    write_register_b(0x06, 0x14); /* subcarrier start timer */
    write_register_b(0x0B, 0x0C); /* P2P receiver */
    write_register_b(kRegBCorrelator1, 0x1B);
    write_register_b(kRegBCorrelator2, 0x00);
    write_register_b(0x0F, 0x00); /* squelch timer */
    write_register_b(0x28, 0x10); /* auxiliary modulation */
    write_register_b(0x29, 0x7C); /* TX driver timing */
    write_register_b(kRegBResistiveAm, 0x80);
    command(kCmdResetRxGain);
    write_interrupt_mask(0);
    return start_field(error);
}

bool St25r3916::field_on_nfcf(std::string& error) {
    error.clear();
    listen_state_ = ListenState::Stopped;

    command(kCmdStopAll);
    modify_register(kRegOperationControl, 0, kOpWakeUp);
    if (!enable_oscillator()) {
        error = failed_ ? "NFC: " + io_error_ : "NFC: the ST25R3916 oscillator did not start";
        return false;
    }
    /* FeliCa changes only a few registers, on top of the NFC-A values. */
    if (field_mode_ == FieldMode::A) {
        save_nfca_registers();
    }
    else {
        restore_nfca_registers();
    }
    field_mode_ = FieldMode::F;

    /* The values M5Stack's library uses for an NFC-F reader: FeliCa with AM, 212 kbit/s both ways. */
    write_register(kRegMode, 0x1C);
    write_register(kRegBitrate, 0x11);
    write_register(kRegAuxiliary, 0x00);
    const uint8_t receiver[4] = {0x13, 0x3D, 0x00, 0x00};
    write_registers(kRegReceiver1, receiver, sizeof(receiver));
    write_register_b(kRegBCorrelator1, 0x54);
    write_register_b(kRegBCorrelator2, 0x00);
    modify_register(kRegTimerControl, 0, kTimerNrtEmv | kTimerNrtStep4096);
    command(kCmdResetRxGain);
    write_interrupt_mask(0);
    return start_field(error);
}

St25r3916::RxStatus St25r3916::exchange_nfcv(const std::vector<uint8_t>& request,
                                             unsigned int timeout_ms,
                                             std::vector<uint8_t>& answer) {
    answer.clear();
    if (!field_on_ || field_mode_ != FieldMode::V || request.empty()) {
        return RxStatus::Error;
    }
    const std::vector<uint8_t> coded = model::iso15693_encode_request(request);
    if (coded.size() > kFifoDepth) {
        return RxStatus::Error;
    }
    prepare_exchange(timeout_ms, false);
    load_fifo(coded.data(), coded.size());
    set_transmit_length(coded.size(), 0);
    command(kCmdTransmitWithoutCrc);

    /* The stream comes in raw: framing errors mean nothing here, the decoder judges it. */
    uint32_t flags = wait_interrupts(kIrqRxEnd | kIrqNoResponse, (timeout_ms + 2) * 1000U);
    if ((flags & kIrqRxEnd) == 0) {
        if ((pending_ & kIrqRxStart) == 0 || failed_) {
            return RxStatus::Timeout;
        }
        if (wait_interrupts(kIrqRxEnd, 20000) == 0) {
            return RxStatus::Error;
        }
    }
    RxFrame rx;
    read_fifo(rx);
    if (failed_ || rx.data.empty()) {
        return RxStatus::Error;
    }
    switch (model::iso15693_decode_response(rx.data.data(), rx.data.size(), answer)) {
        case model::Iso15693Decode::Ok:
            return RxStatus::Ok;
        case model::Iso15693Decode::Collision:
            return RxStatus::Collision;
        default:
            return RxStatus::Error;
    }
}

bool St25r3916::start_field(std::string& error) {
    /* Field on with collision avoidance: the chip first checks that no other field is present. */
    command(kCmdStopAll);
    modify_register(kRegOperationControl,
                    0,
                    kOpTxEnable | kOpRxEnable | kOpWakeUp | kOpFieldDetectorMask);
    modify_register(kRegOperationControl, kOpFieldDetectorCollisionAvoidance, kOpFieldDetectorMask);
    /* Guard time before the first command: 75 us + 33 * 151 us = 5.06 ms. */
    write_register_b(kRegBFieldOnGuardTimer, 33);
    clear_interrupts();
    command(kCmdInitialFieldOn);

    const uint32_t flags = wait_interrupts(kIrqFieldGuardDone | kIrqFieldCollision, 15000);
    if (failed_) {
        error = "NFC: " + io_error_;
        return false;
    }
    if ((flags & kIrqFieldCollision) != 0) {
        error = "Another reader's field is present";
        return false;
    }
    if ((flags & kIrqFieldGuardDone) == 0 ||
        (read_register(kRegOperationControl) & kOpTxEnable) == 0) {
        error = "NFC: the field did not turn on";
        return false;
    }
    modify_register(kRegOperationControl, kOpRxEnable, kOpWakeUp);
    field_on_ = !failed_;
    if (failed_) {
        error = "NFC: " + io_error_;
    }
    return field_on_;
}

void St25r3916::field_off() {
    command(kCmdStopAll);
    modify_register(kRegOperationControl, 0, kOpTxEnable | kOpRxEnable);
    field_on_ = false;
}

void St25r3916::load_fifo(const uint8_t* data, std::size_t size) {
    std::vector<uint8_t> tx(size + 1);
    tx[0] = kOpLoadFifo;
    std::copy_n(data, size, tx.begin() + 1);
    xfer(tx);
}

void St25r3916::set_transmit_length(std::size_t bytes, uint8_t bits) {
    const uint16_t value = static_cast<uint16_t>((bytes << 3) | (bits & 0x07));
    const uint8_t registers[2] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    write_registers(kRegTransmitBytes1, registers, sizeof(registers));
}

void St25r3916::read_fifo(RxFrame& rx) {
    rx.data.clear();
    rx.last_bits = 0;

    std::size_t bytes = 0;
    uint8_t status2 = 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto status = xfer({static_cast<uint8_t>(kOpReadRegister | kRegFifoStatus1), 0, 0});
        status2 = status[2];
        bytes = static_cast<std::size_t>(status[1]) | (static_cast<std::size_t>(status2 & 0xC0) << 2);
        if (bytes != 0 || failed_) {
            break;
        }
        sleep_ms(1);
    }
    bytes = std::min(bytes, kFifoDepth);
    if (bytes == 0) {
        return;
    }

    std::vector<uint8_t> tx(bytes + 1, 0);
    tx[0] = kOpReadFifo;
    const auto data = xfer(tx);
    rx.data.assign(data.begin() + 1, data.end());
    rx.last_bits = static_cast<uint8_t>((status2 >> 1) & 0x07);
}

void St25r3916::prepare_exchange(unsigned int timeout_ms, bool anticollision) {
    command(kCmdStopAll);
    command(kCmdResetRxGain);
    const uint16_t ticks = no_response_ticks(timeout_ms);
    const uint8_t timer[2] = {static_cast<uint8_t>(ticks >> 8), static_cast<uint8_t>(ticks)};
    write_registers(kRegNoResponseTimer1, timer, sizeof(timer));
    write_register(kRegIso14443a, anticollision ? kIsoAnticollision : 0x00);
    /* The CRC is left in the frame and checked by the caller. */
    modify_register(kRegAuxiliary, kAuxNoCrcRx, 0);
    clear_interrupts();
    command(kCmdClearFifo);
}

St25r3916::RxStatus St25r3916::receive(unsigned int timeout_ms, RxFrame& rx) {
    rx.data.clear();
    rx.last_bits = 0;

    uint32_t flags = wait_interrupts(kIrqRxEnd | kIrqCollision | kIrqNoResponse, (timeout_ms + 2) * 1000U);
    if ((flags & (kIrqRxEnd | kIrqCollision)) == 0) {
        /* Nothing complete yet. If a frame has started, give it time to end. */
        if ((pending_ & kIrqRxStart) == 0 || failed_) {
            return RxStatus::Timeout;
        }
        flags = wait_interrupts(kIrqRxEnd | kIrqCollision, 20000);
        if (flags == 0) {
            return RxStatus::Error;
        }
    }

    const uint32_t errors = pending_ & kIrqRxErrors;
    read_fifo(rx);
    if (failed_) {
        return RxStatus::Error;
    }
    if ((flags & kIrqCollision) != 0) {
        return RxStatus::Collision;
    }
    if (rx.data.empty()) {
        return RxStatus::Error;
    }
    /* A lone half byte is a Type 2 acknowledge: it has no parity bit to check. */
    const bool nibble = rx.data.size() == 1 && rx.last_bits == 4;
    if (!nibble && (errors & (kIrqParityError | kIrqHardFramingError)) != 0) {
        return RxStatus::Error;
    }
    return RxStatus::Ok;
}

St25r3916::RxStatus St25r3916::wake_up(RxFrame& rx) {
    if (!field_on_) {
        return RxStatus::Error;
    }
    prepare_exchange(4, false);
    set_transmit_length(0, 0);
    command(kCmdTransmitWupa);
    return receive(4, rx);
}

St25r3916::RxStatus St25r3916::exchange(const uint8_t* tx,
                                        std::size_t size,
                                        bool append_crc,
                                        bool anticollision,
                                        unsigned int timeout_ms,
                                        RxFrame& rx) {
    rx.data.clear();
    rx.last_bits = 0;
    if (!field_on_ || !tx || size == 0 || size > kFifoDepth) {
        return RxStatus::Error;
    }
    prepare_exchange(timeout_ms, anticollision);
    load_fifo(tx, size);
    set_transmit_length(size, 0);
    command(append_crc ? kCmdTransmitWithCrc : kCmdTransmitWithoutCrc);
    return receive(timeout_ms, rx);
}

void St25r3916::transmit(const uint8_t* tx, std::size_t size, bool append_crc) {
    if (!field_on_ || !tx || size == 0 || size > kFifoDepth) {
        return;
    }
    prepare_exchange(1, false);
    load_fifo(tx, size);
    set_transmit_length(size, 0);
    command(append_crc ? kCmdTransmitWithCrc : kCmdTransmitWithoutCrc);
    wait_interrupts(kIrqTxEnd, 5000);
    sleep_ms(1);
}

/* ---- Listener -------------------------------------------------------------- */

/*
 * The listener follows amiibo_sim (MIT, Copyright (c) 2026 amiibo_sim
 * contributors, github.com/RiviaRammer/CardputerZeroApps), which emulates
 * NTAG215 tags reliably on this same Cap. Its approach: set the chip up once
 * as a fixed NFC-A target with the CRC check on and the oscillator running,
 * let it answer REQA, anticollision and SELECT entirely by itself, and touch
 * it only once it reports a completed selection. Every register write while
 * the chip runs its own anticollision risks spoiling an answer.
 */

namespace {

/* Passive target definition: FDT adjustment 5, AP2P and NFC-F off; NFC-A answered by the chip, or not. */
constexpr uint8_t kPtNfcAAutomatic = 0x5C;
constexpr uint8_t kPtNfcAManual = 0x5D;

/* The interrupts the listener uses, in the order of the four interrupt registers. */
constexpr uint8_t kListenMainIrqs = 0x40 /* FIFO water level */ | 0x20 /* RX start */ | 0x10 /* RX end */ |
                                    0x08 /* TX end */;
constexpr uint8_t kListenTimerIrqs = 0x10 /* field on */ | 0x08 /* field off */;
constexpr uint8_t kListenTargetIrqs = 0x10 /* automatic response RX end */ | 0x03 /* selected, from idle or halt */;

constexpr uint8_t kMainRxStart = 0x20;
constexpr uint8_t kMainRxEnd = 0x10;
constexpr uint8_t kTimerFieldOn = 0x10;
constexpr uint8_t kTimerFieldOff = 0x08;
constexpr uint8_t kTargetAutoRx = 0x10;
constexpr uint8_t kTargetSelected = 0x03;
constexpr uint8_t kErrorRx = 0xF0;

/* Passive target display: the chip's anticollision state. */
constexpr uint8_t kPtStateActive = 0x05;
constexpr uint8_t kPtStateActiveFromHalt = 0x0D;

/* Longest frame a reader sends a selected Type 2 tag, with room to spare. */
constexpr std::size_t kListenMaxFrame = 64;
/* With the field on and no interrupt for this long, the field is checked directly. */
constexpr unsigned int kListenFieldCheckUs = 100000;

} // namespace

bool St25r3916::listen_start(const std::vector<uint8_t>& uid,
                             const std::array<uint8_t, 2>& atqa,
                             uint8_t sak,
                             std::string& error) {
    error.clear();
    if (uid.size() != 4 && uid.size() != 7) {
        error = "Only tags with a 4- or 7-byte UID can be emulated";
        return false;
    }
    const bool uid7 = uid.size() == 7;

    field_off();
    write_interrupt_mask(0xFFFFFFFFU);

    /* 3.3 V supply range, antenna tuning DAC on, SPI pull-downs: what amiibo_sim uses on this Cap. */
    const uint8_t io_configuration[2] = {0x07, 0xBC};
    write_registers(kRegIoConfiguration1, io_configuration, sizeof(io_configuration));
    write_register(kRegOperationControl, kOpEnable | kOpFieldDetectorAutomatic);
    bool oscillator = false;
    for (int attempt = 0; attempt < 50 && !failed_; ++attempt) {
        if ((read_register(kRegAuxiliaryDisplay) & kAuxDisplayOscillatorOk) != 0) {
            oscillator = true;
            break;
        }
        sleep_ms(1);
    }
    if (!oscillator) {
        error = failed_ ? "NFC: " + io_error_ : "NFC: the ST25R3916 oscillator did not start";
        return false;
    }
    write_register(kRegRegulatorVoltageControl, 0x80);
    write_register(kRegRegulatorVoltageControl, 0x00);
    command(kCmdAdjustRegulators);
    sleep_ms(5);

    /* Fixed NFC-A target at 106 kbit/s, CRC checked by the chip, for the whole emulation. */
    write_register(kRegMode, kModeTargetNfcA);
    write_register(kRegAuxiliary, uid7 ? kAuxUid7 : 0x00);

    /*
     * The identity the chip answers with on its own: UID (padded to ten
     * bytes), ATQA, and the SAK of each cascade level. A 7-byte UID reports
     * "not complete" at the first level.
     */
    std::vector<uint8_t> memory(16, 0);
    memory[0] = kOpLoadPtMemoryA;
    std::copy(uid.begin(), uid.end(), memory.begin() + 1);
    memory[11] = atqa[0];
    memory[12] = atqa[1];
    const uint8_t complete = static_cast<uint8_t>(sak & ~0x04);
    memory[13] = uid7 ? static_cast<uint8_t>(sak | 0x04) : complete;
    memory[14] = complete;
    memory[15] = complete;
    xfer(memory);

    write_register(kRegBitrate, 0x00);
    write_register(kRegIso14443a, 0x00);
    write_register(kRegTimerControl, kTimerMrtStep512);
    /* Mask receive timer: three steps of 512 carrier periods, about 113 us. */
    write_register(kRegMaskReceiveTimer, 3);
    write_register(kRegAntennaTuning1, 0x00);
    write_register(kRegAntennaTuning2, 0xFF);
    write_register_b(kRegBOvershoot1, 0x00);
    write_register_b(kRegBOvershoot2, 0x00);
    write_register_b(kRegBUndershoot1, 0x00);
    write_register_b(kRegBUndershoot2, 0x00);

    const uint32_t enabled = (static_cast<uint32_t>(kListenMainIrqs) << 24) |
                             (static_cast<uint32_t>(kListenTimerIrqs) << 16) | kListenTargetIrqs;
    write_interrupt_mask(~enabled);
    write_register(kRegOperationControl, kOpEnable | kOpRxEnable | kOpFieldDetectorAutomatic);
    clear_interrupts();

    listen_stats_ = ListenStats{};
    listen_stats_.bitrate = 0;
    listen_state_ = ListenState::Listening;
    listen_arm();
    listen_field_ = external_field();
    listen_selected_in_field_ = false;
    listen_quiet_since_ = std::chrono::steady_clock::now();

    if (failed_) {
        error = "NFC: " + io_error_;
        listen_state_ = ListenState::Stopped;
        return false;
    }
    return true;
}

void St25r3916::listen_arm() {
    /* Waiting for REQA again, with the chip answering by itself. */
    listen_halted_ = false;
    if (listen_state_ != ListenState::Stopped) {
        listen_state_ = ListenState::Listening;
    }
    command(kCmdStopAll);
    write_register(kRegPassiveTarget, kPtNfcAAutomatic);
    command(kCmdGoToSense);
    command(kCmdUnmaskReceiveData);
}

void St25r3916::listen_stop() {
    if (listen_state_ == ListenState::Stopped) {
        return;
    }
    listen_state_ = ListenState::Stopped;
    command(kCmdStopAll);
    write_interrupt_mask(0xFFFFFFFFU);
    write_register(kRegPassiveTarget, kPtNfcAAutomatic | kPtDisableNfcA);
    write_register(kRegMode, 0x00);
    modify_register(kRegOperationControl, 0, kOpTxEnable | kOpRxEnable);
}

bool St25r3916::listen_field_present() const {
    return listen_state_ != ListenState::Stopped && listen_field_;
}

const St25r3916::ListenStats& St25r3916::listen_stats() const {
    return listen_stats_;
}

bool St25r3916::external_field() {
    return (read_register(kRegAuxiliaryDisplay) & kAuxDisplayExternalField) != 0;
}

void St25r3916::listen_field_gone() {
    /* The chip returns to its field-off state by itself; only a selection needs undoing. */
    const bool rearm = listen_selected_in_field_;
    listen_selected_in_field_ = false;
    listen_field_ = false;
    if (rearm) {
        listen_arm();
    }
}

void St25r3916::listen_end_session(bool halt) {
    if (listen_state_ != ListenState::Active) {
        return;
    }
    if (halt) {
        /* HLTA: back to the chip's own answers, asleep until a WUPA. */
        listen_state_ = ListenState::Listening;
        listen_halted_ = true;
        write_register(kRegPassiveTarget, kPtNfcAAutomatic);
        command(kCmdGoToSleep);
    }
    /* Otherwise the tag stays selected and silent, waiting for the next command. */
    command(kCmdUnmaskReceiveData);
}

void St25r3916::listen_reply(const uint8_t* data, std::size_t size) {
    if (listen_state_ != ListenState::Active || !data || size == 0 || size > kFifoDepth - 2) {
        return;
    }
    send_reply(data, size, 0, kCmdTransmitWithCrc);
}

void St25r3916::listen_reply_nibble(uint8_t nibble) {
    if (listen_state_ != ListenState::Active) {
        return;
    }
    const uint8_t value = static_cast<uint8_t>(nibble & 0x0F);
    send_reply(&value, 0, 4, kCmdTransmitWithoutCrc);
}

void St25r3916::send_reply(const uint8_t* data, std::size_t bytes, uint8_t bits, uint8_t transmit) {
    /* Clear, load, length and send in one system call. */
    const std::size_t loaded = bytes + (bits != 0 ? 1 : 0);
    std::vector<uint8_t> fifo(loaded + 1);
    fifo[0] = kOpLoadFifo;
    std::copy_n(data, loaded, fifo.begin() + 1);
    const uint16_t length = static_cast<uint16_t>((bytes << 3) | (bits & 0x07));
    write_frames({{kCmdClearFifo},
                  std::move(fifo),
                  {kRegTransmitBytes1, static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length)},
                  {transmit}});
}

bool St25r3916::listen_frame(uint8_t errors, bool just_selected, std::vector<uint8_t>& frame) {
    const auto status = xfer({static_cast<uint8_t>(kOpReadRegister | kRegFifoStatus1), 0, 0});
    const std::size_t length = static_cast<std::size_t>(status[1]) | (static_cast<std::size_t>(status[2] & 0xC0) << 2);
    if (just_selected && length == 0) {
        return false;
    }
    if (errors != 0 || (status[2] & 0x3F) != 0 || length <= 2 || length > kListenMaxFrame) {
        ++listen_stats_.bad_frames;
        command(kCmdClearFifo);
        command(kCmdUnmaskReceiveData);
        return false;
    }
    std::vector<uint8_t> tx(length + 1, 0);
    tx[0] = kOpReadFifo;
    const auto data = xfer(tx);
    if (failed_) {
        return false;
    }
    /* The last SELECT can still be in the FIFO when the selection is reported. */
    if (just_selected && (data[1] == 0x93 || data[1] == 0x95 || data[1] == 0x97) && data[2] == 0x70) {
        return false;
    }
    /* The chip has checked the CRC; the two bytes are dropped. */
    frame.assign(data.begin() + 1, data.end() - 2);
    ++listen_stats_.frames;
    return true;
}

bool St25r3916::listen_handle_irq(std::vector<uint8_t>& frame) {
    /* The error register first: reading the main register can clear its status. */
    const uint8_t errors = xfer({static_cast<uint8_t>(kOpReadRegister | 0x1C), 0})[1];
    const auto main = xfer({static_cast<uint8_t>(kOpReadRegister | kRegInterrupt), 0, 0});
    const uint8_t target = xfer({static_cast<uint8_t>(kOpReadRegister | 0x1D), 0})[1];
    if (failed_) {
        return false;
    }
    const uint8_t status = main[1];
    const uint8_t timer = main[2];
    listen_quiet_since_ = std::chrono::steady_clock::now();

    if ((timer & kTimerFieldOff) != 0) {
        listen_field_gone();
        return false;
    }
    if ((timer & kTimerFieldOn) != 0) {
        ++listen_stats_.fields;
    }
    if ((target & kTargetAutoRx) != 0) {
        ++listen_stats_.attempts;
    }
    if ((timer & kTimerFieldOn) != 0 || (target & (kTargetAutoRx | kTargetSelected)) != 0) {
        listen_field_ = true;
    }

    const bool active = listen_state_ == ListenState::Active;
    bool just_selected = !active && (target & kTargetSelected) != 0;
    if (!active && !just_selected && (status & (kMainRxStart | kMainRxEnd)) != 0) {
        /* The selection and the reader's first command can arrive in separate readings. */
        const uint8_t state = read_register(kRegPassiveTargetDisplay) & 0x0F;
        just_selected = listen_halted_ ? state == kPtStateActiveFromHalt
                                       : (state == kPtStateActive || state == kPtStateActiveFromHalt);
    }
    if (just_selected) {
        listen_state_ = ListenState::Active;
        listen_halted_ = false;
        listen_field_ = true;
        listen_selected_in_field_ = true;
        ++listen_stats_.selections;
        /* Selected: the chip stops answering by itself and passes frames on. */
        write_register(kRegPassiveTarget, kPtNfcAManual);
        if ((status & (kMainRxStart | kMainRxEnd)) == 0) {
            command(kCmdUnmaskReceiveData);
        }
    }

    if (listen_state_ == ListenState::Active && (status & kMainRxEnd) != 0) {
        return listen_frame(static_cast<uint8_t>(errors & kErrorRx), just_selected, frame);
    }
    return false;
}

bool St25r3916::listen_poll(unsigned int timeout_ms, std::vector<uint8_t>& frame) {
    frame.clear();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        if (failed_ || listen_state_ == ListenState::Stopped) {
            return false;
        }
        /* The interrupt line stays high until the registers are read, edge or not. */
        if (irq_->active()) {
            if (listen_handle_irq(frame)) {
                return true;
            }
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        if (listen_field_ && now - listen_quiet_since_ >= std::chrono::microseconds(kListenFieldCheckUs)) {
            listen_quiet_since_ = now;
            if (!external_field()) {
                listen_field_gone();
            }
        }
        if (now >= deadline) {
            return false;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - now).count();
        irq_->wait(static_cast<unsigned int>(std::min<long long>(remaining, kListenFieldCheckUs)));
    }
}

} // namespace platform
