/*
 * SPDX-License-Identifier: MIT
 *
 * Register sequences follow the Bosch Sensortec BMI270 and BMM150 Sensor APIs
 * (BSD-3-Clause); the BMM150 compensation is their floating-point version.
 */

#include "imu_sensors.h"

#include "bmi270_config.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace platform {
namespace {

/* BMI270 registers. */
constexpr uint8_t kBmiChipId = 0x00;
constexpr uint8_t kBmiStatus = 0x03;
constexpr uint8_t kBmiAuxData = 0x04;
constexpr uint8_t kBmiAccelData = 0x0C;
constexpr uint8_t kBmiInternalStatus = 0x21;
constexpr uint8_t kBmiTemperature = 0x22;
constexpr uint8_t kBmiAccConf = 0x40;
constexpr uint8_t kBmiAccRange = 0x41;
constexpr uint8_t kBmiGyrConf = 0x42;
constexpr uint8_t kBmiGyrRange = 0x43;
constexpr uint8_t kBmiAuxDevId = 0x4B;
constexpr uint8_t kBmiAuxIfConf = 0x4C;
constexpr uint8_t kBmiAuxRdAddr = 0x4D;
constexpr uint8_t kBmiAuxWrAddr = 0x4E;
constexpr uint8_t kBmiAuxWrData = 0x4F;
constexpr uint8_t kBmiInitCtrl = 0x59;
constexpr uint8_t kBmiInitAddr0 = 0x5B;
constexpr uint8_t kBmiInitData = 0x5E;
constexpr uint8_t kBmiIfConf = 0x6B;
constexpr uint8_t kBmiPwrConf = 0x7C;
constexpr uint8_t kBmiPwrCtrl = 0x7D;
constexpr uint8_t kBmiCmd = 0x7E;

constexpr uint8_t kBmiSoftReset = 0xB6;
constexpr uint8_t kBmiAuxBusy = 0x04;
constexpr uint8_t kBmiAuxManualMode = 0x80;
constexpr uint8_t kBmiIfConfAuxEnable = 0x20;
/* PWR_CTRL bits: aux, gyroscope, accelerometer, temperature. */
constexpr uint8_t kBmiPowerAux = 0x01;
constexpr uint8_t kBmiPowerMotion = 0x0E;

/* Bytes per config upload burst; must be even. */
constexpr std::size_t kConfigChunk = 64;

/* +-4 g and +-2000 dps over the int16 range. */
constexpr float kAccelLsbPerG = 32768.0F / 4.0F;
constexpr float kGyroLsbPerDps = 32768.0F / 2000.0F;

/* BMM150 registers. */
constexpr uint8_t kBmmChipId = 0x40;
constexpr uint8_t kBmmData = 0x42;
constexpr uint8_t kBmmPowerCtrl = 0x4B;
constexpr uint8_t kBmmOpMode = 0x4C;
constexpr uint8_t kBmmRepXy = 0x51;
constexpr uint8_t kBmmRepZ = 0x52;
constexpr uint8_t kBmmDigX1 = 0x5D;
constexpr uint8_t kBmmDigZ4 = 0x62;
constexpr uint8_t kBmmDigZ2 = 0x68;

constexpr int16_t kBmmOverflowXy = -4096;
constexpr int16_t kBmmOverflowZ = -16384;

void sleep_ms(unsigned int duration_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
}

int16_t le16(const uint8_t* data) {
    return static_cast<int16_t>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8));
}

} // namespace

/* ---- BMI270 ------------------------------------------------------------------ */

bool Bmi270::probe(i2c::Device& device) {
    uint8_t chip_id = 0;
    return device.read_byte(kBmiChipId, chip_id) && chip_id == kChipId;
}

Bmi270::Bmi270(std::unique_ptr<i2c::Device> device) : device_(std::move(device)) {}

const i2c::Device& Bmi270::device() const {
    return *device_;
}

bool Bmi270::init(std::string& error) {
    if (!probe(*device_)) {
        error = "BMI270 does not answer";
        return false;
    }

    /* The reset is not acknowledged reliably, so its result is ignored. */
    device_->write_byte(kBmiCmd, kBmiSoftReset);
    sleep_ms(5);

    /* Advanced power save off: no 450 us gap needed between writes from here on. */
    if (!device_->write_byte(kBmiPwrConf, 0x00)) {
        error = "BMI270 power config failed";
        return false;
    }
    sleep_ms(1);

    if (!device_->write_byte(kBmiInitCtrl, 0x00)) {
        error = "BMI270 init control failed";
        return false;
    }
    for (std::size_t index = 0; index < kBmi270ConfigFileSize; index += kConfigChunk) {
        const std::size_t length = std::min(kConfigChunk, kBmi270ConfigFileSize - index);
        /* The load address counts 16-bit words: bits 0-3 in INIT_ADDR_0, bits 4-11 in INIT_ADDR_1. */
        const uint8_t address[2] = {static_cast<uint8_t>((index / 2) & 0x0F),
                                    static_cast<uint8_t>((index / 2) >> 4)};
        if (!device_->write(kBmiInitAddr0, address, sizeof(address)) ||
            !device_->write(kBmiInitData, kBmi270ConfigFile + index, length)) {
            error = "BMI270 config upload failed";
            return false;
        }
    }
    if (!device_->write_byte(kBmiInitCtrl, 0x01)) {
        error = "BMI270 init control failed";
        return false;
    }

    /* The datasheet allows 20 ms for the feature engine to start. */
    uint8_t status = 0;
    bool initialized = false;
    for (int attempt = 0; attempt < 15 && !initialized; ++attempt) {
        sleep_ms(10);
        initialized = device_->read_byte(kBmiInternalStatus, status) && (status & 0x0F) == 0x01;
    }
    if (!initialized) {
        error = "BMI270 config load failed (status " + std::to_string(status & 0x0F) + ")";
        return false;
    }

    /* Accel: performance filter, normal bandwidth, 100 Hz, +-4 g. Gyro: performance, 200 Hz, +-2000 dps. */
    const bool configured = device_->write_byte(kBmiAccConf, 0xA8) &&
                            device_->write_byte(kBmiAccRange, 0x01) &&
                            device_->write_byte(kBmiGyrConf, 0xA9) &&
                            device_->write_byte(kBmiGyrRange, 0x00) &&
                            device_->write_byte(kBmiPwrCtrl, kBmiPowerMotion);
    if (!configured) {
        error = "BMI270 sensor setup failed";
        return false;
    }
    /* First samples arrive after the gyro start-up time. */
    sleep_ms(50);
    return true;
}

bool Bmi270::read_motion(std::array<float, 3>& accel_g, std::array<float, 3>& gyro_dps) {
    uint8_t data[12];
    if (!device_->read(kBmiAccelData, data, sizeof(data))) {
        return false;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        accel_g[axis] = static_cast<float>(le16(data + axis * 2)) / kAccelLsbPerG;
        gyro_dps[axis] = static_cast<float>(le16(data + 6 + axis * 2)) / kGyroLsbPerDps;
    }
    return true;
}

bool Bmi270::read_temperature(float& celsius) {
    uint8_t data[2];
    if (!device_->read(kBmiTemperature, data, sizeof(data))) {
        return false;
    }
    const int16_t raw = le16(data);
    if (raw == INT16_MIN) {
        return false;
    }
    /* 0 is 23 degC, one LSB is 1/512 K. */
    celsius = 23.0F + static_cast<float>(raw) / 512.0F;
    return true;
}

bool Bmi270::wait_aux_idle() {
    for (int attempt = 0; attempt < 50; ++attempt) {
        uint8_t status = 0;
        if (!device_->read_byte(kBmiStatus, status)) {
            return false;
        }
        if ((status & kBmiAuxBusy) == 0) {
            return true;
        }
        sleep_ms(1);
    }
    return false;
}

bool Bmi270::enable_aux(uint8_t address) {
    uint8_t power = 0;
    uint8_t if_conf = 0;
    if (!device_->read_byte(kBmiPwrCtrl, power) ||
        !device_->write_byte(kBmiPwrCtrl, static_cast<uint8_t>(power | kBmiPowerAux)) ||
        !device_->read_byte(kBmiIfConf, if_conf) ||
        !device_->write_byte(kBmiIfConf, static_cast<uint8_t>(if_conf | kBmiIfConfAuxEnable))) {
        return false;
    }
    sleep_ms(1);
    if (!wait_aux_idle()) {
        return false;
    }
    /* The device ID register holds the 7-bit address in bits 1-7. */
    aux_if_conf_ = kBmiAuxManualMode;
    if (!device_->write_byte(kBmiAuxDevId, static_cast<uint8_t>(address << 1)) ||
        !device_->write_byte(kBmiAuxIfConf, aux_if_conf_)) {
        return false;
    }
    sleep_ms(1);
    return true;
}

bool Bmi270::set_aux_burst(std::size_t length) {
    /* Manual read burst: 0 = 1 byte, 1 = 2, 2 = 6, 3 = 8. */
    const uint8_t code = length <= 1 ? 0 : length <= 2 ? 1 : length <= 6 ? 2 : 3;
    const uint8_t wanted = static_cast<uint8_t>(kBmiAuxManualMode | (code << 2));
    if (wanted == aux_if_conf_) {
        return true;
    }
    if (!wait_aux_idle() || !device_->write_byte(kBmiAuxIfConf, wanted)) {
        return false;
    }
    aux_if_conf_ = wanted;
    return true;
}

bool Bmi270::aux_read(uint8_t reg, uint8_t* data, std::size_t length) {
    while (length > 0) {
        const std::size_t chunk = std::min<std::size_t>(length, 8);
        /* Writing the read address starts the transfer on the aux bus. */
        if (!set_aux_burst(chunk) || !wait_aux_idle() || !device_->write_byte(kBmiAuxRdAddr, reg) ||
            !wait_aux_idle() || !device_->read(kBmiAuxData, data, chunk)) {
            return false;
        }
        reg = static_cast<uint8_t>(reg + chunk);
        data += chunk;
        length -= chunk;
    }
    return true;
}

bool Bmi270::aux_write(uint8_t reg, uint8_t value) {
    /* Writing the write address sends the data byte staged before it. */
    return wait_aux_idle() && device_->write_byte(kBmiAuxWrData, value) &&
           device_->write_byte(kBmiAuxWrAddr, reg) && wait_aux_idle();
}

/* ---- BMM150 ------------------------------------------------------------------ */

DirectMagnetometerBus::DirectMagnetometerBus(std::unique_ptr<i2c::Device> device) : device_(std::move(device)) {}

bool DirectMagnetometerBus::read(uint8_t reg, uint8_t* data, std::size_t length) {
    return device_->read(reg, data, length);
}

bool DirectMagnetometerBus::write(uint8_t reg, uint8_t value) {
    return device_->write_byte(reg, value);
}

AuxMagnetometerBus::AuxMagnetometerBus(Bmi270& host) : host_(host) {}

bool AuxMagnetometerBus::read(uint8_t reg, uint8_t* data, std::size_t length) {
    return host_.aux_read(reg, data, length);
}

bool AuxMagnetometerBus::write(uint8_t reg, uint8_t value) {
    return host_.aux_write(reg, value);
}

Bmm150::Bmm150(std::unique_ptr<MagnetometerBus> bus) : bus_(std::move(bus)) {}

bool Bmm150::probe() {
    /* Suspend to sleep: only the power control register answers in suspend. */
    if (!bus_->write(kBmmPowerCtrl, 0x01)) {
        return false;
    }
    sleep_ms(4);
    uint8_t chip_id = 0;
    return bus_->read(kBmmChipId, &chip_id, 1) && chip_id == kChipId;
}

bool Bmm150::read_trim() {
    uint8_t x1y1[2];
    uint8_t xyz[4];
    uint8_t xy1xy2[10];
    if (!bus_->read(kBmmDigX1, x1y1, sizeof(x1y1)) || !bus_->read(kBmmDigZ4, xyz, sizeof(xyz)) ||
        !bus_->read(kBmmDigZ2, xy1xy2, sizeof(xy1xy2))) {
        return false;
    }
    trim_.x1 = static_cast<int8_t>(x1y1[0]);
    trim_.y1 = static_cast<int8_t>(x1y1[1]);
    trim_.x2 = static_cast<int8_t>(xyz[2]);
    trim_.y2 = static_cast<int8_t>(xyz[3]);
    trim_.z1 = static_cast<uint16_t>(xy1xy2[2] | (xy1xy2[3] << 8));
    trim_.z2 = le16(xy1xy2);
    trim_.z3 = le16(xy1xy2 + 6);
    trim_.z4 = le16(xyz);
    trim_.xy1 = xy1xy2[9];
    trim_.xy2 = static_cast<int8_t>(xy1xy2[8]);
    trim_.xyz1 = static_cast<uint16_t>(xy1xy2[4] | ((xy1xy2[5] & 0x7F) << 8));
    return true;
}

bool Bmm150::init(std::string& error) {
    if (!probe()) {
        error = "BMM150 does not answer";
        return false;
    }
    if (!read_trim()) {
        error = "BMM150 trim read failed";
        return false;
    }
    /* Regular preset: 9 XY and 15 Z repetitions. Normal mode at 20 Hz (ODR code 5). */
    if (!bus_->write(kBmmRepXy, 0x04) || !bus_->write(kBmmRepZ, 0x0E) ||
        !bus_->write(kBmmOpMode, static_cast<uint8_t>(0x05 << 3))) {
        error = "BMM150 setup failed";
        return false;
    }
    sleep_ms(60);
    return true;
}

bool Bmm150::read_field(std::array<float, 3>& field_ut) {
    uint8_t data[8];
    if (!bus_->read(kBmmData, data, sizeof(data))) {
        return false;
    }
    /* X and Y are 13 bits in bits 3-15, Z 15 bits in bits 1-15, RHALL 14 bits in bits 2-15. */
    const auto raw_x = static_cast<int16_t>(le16(data) >> 3);
    const auto raw_y = static_cast<int16_t>(le16(data + 2) >> 3);
    const auto raw_z = static_cast<int16_t>(le16(data + 4) >> 1);
    const auto rhall = static_cast<uint16_t>((data[6] | (data[7] << 8)) >> 2);

    if (rhall == 0 || trim_.xyz1 == 0 || raw_x == kBmmOverflowXy || raw_y == kBmmOverflowXy ||
        raw_z == kBmmOverflowZ || trim_.z1 == 0 || trim_.z2 == 0) {
        return false;
    }

    const float hall = static_cast<float>(rhall);
    const float base = static_cast<float>(trim_.xyz1) * 16384.0F / hall - 16384.0F;
    const float common = static_cast<float>(trim_.xy2) * (base * base / 268435456.0F) +
                         base * static_cast<float>(trim_.xy1) / 16384.0F + 256.0F;

    const float x = static_cast<float>(raw_x) * common * (static_cast<float>(trim_.x2) + 160.0F);
    field_ut[0] = (x / 8192.0F + static_cast<float>(trim_.x1) * 8.0F) / 16.0F;
    const float y = static_cast<float>(raw_y) * common * (static_cast<float>(trim_.y2) + 160.0F);
    field_ut[1] = (y / 8192.0F + static_cast<float>(trim_.y1) * 8.0F) / 16.0F;

    const float z0 = static_cast<float>(raw_z) - static_cast<float>(trim_.z4);
    const float z2 = static_cast<float>(trim_.z3) * (hall - static_cast<float>(trim_.xyz1));
    const float z4 = static_cast<float>(trim_.z2) + static_cast<float>(trim_.z1) * hall / 32768.0F;
    field_ut[2] = ((z0 * 131072.0F - z2) / (z4 * 4.0F)) / 16.0F;
    return true;
}

} // namespace platform
