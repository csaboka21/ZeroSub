/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "i2c_device.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace platform {

/*
 * Bosch BMI270 6-axis IMU on I2C (0x68 or 0x69). Runs the accelerometer at
 * 100 Hz, +-4 g and the gyroscope at 200 Hz, +-2000 dps. Its auxiliary
 * interface can reach a magnetometer wired behind it.
 */
class Bmi270 {
public:
    static constexpr uint8_t kChipId = 0x24;

    /* Checks the chip ID only; nothing is written. */
    static bool probe(i2c::Device& device);

    explicit Bmi270(std::unique_ptr<i2c::Device> device);

    /* Soft reset, config upload (about 8 KB over I2C) and sensor setup. */
    bool init(std::string& error);
    /* Acceleration in g, rotation in degrees per second. */
    bool read_motion(std::array<float, 3>& accel_g, std::array<float, 3>& gyro_dps);
    /* Die temperature; false while the chip has no valid reading. */
    bool read_temperature(float& celsius);

    /*
     * Auxiliary interface in manual mode, for a magnetometer at `address`.
     * enable_aux() must come before aux_read() / aux_write().
     */
    bool enable_aux(uint8_t address);
    bool aux_read(uint8_t reg, uint8_t* data, std::size_t length);
    bool aux_write(uint8_t reg, uint8_t value);

    const i2c::Device& device() const;

private:
    bool wait_aux_idle();
    bool set_aux_burst(std::size_t length);

    std::unique_ptr<i2c::Device> device_;
    uint8_t aux_if_conf_{0};
};

/* Register access to a BMM150, either straight on the bus or through the BMI270. */
class MagnetometerBus {
public:
    virtual ~MagnetometerBus() = default;
    virtual bool read(uint8_t reg, uint8_t* data, std::size_t length) = 0;
    virtual bool write(uint8_t reg, uint8_t value) = 0;
};

/* Bosch BMM150 3-axis magnetometer: 20 Hz normal mode, regular preset, trim-compensated. */
class Bmm150 {
public:
    static constexpr uint8_t kChipId = 0x32;

    explicit Bmm150(std::unique_ptr<MagnetometerBus> bus);

    /* Wakes the chip from suspend and checks the chip ID. Safe on an empty address. */
    bool probe();
    bool init(std::string& error);
    /* Field in microtesla. False on a bus error or an overflowing reading. */
    bool read_field(std::array<float, 3>& field_ut);

private:
    struct Trim {
        int8_t x1{0};
        int8_t y1{0};
        int8_t x2{0};
        int8_t y2{0};
        uint16_t z1{0};
        int16_t z2{0};
        int16_t z3{0};
        int16_t z4{0};
        uint8_t xy1{0};
        int8_t xy2{0};
        uint16_t xyz1{0};
    };

    bool read_trim();

    std::unique_ptr<MagnetometerBus> bus_;
    Trim trim_;
};

/* A BMM150 that answers on the bus itself. */
class DirectMagnetometerBus : public MagnetometerBus {
public:
    explicit DirectMagnetometerBus(std::unique_ptr<i2c::Device> device);
    bool read(uint8_t reg, uint8_t* data, std::size_t length) override;
    bool write(uint8_t reg, uint8_t value) override;

private:
    std::unique_ptr<i2c::Device> device_;
};

/* A BMM150 on the BMI270's auxiliary interface. The BMI270 must outlive it. */
class AuxMagnetometerBus : public MagnetometerBus {
public:
    explicit AuxMagnetometerBus(Bmi270& host);
    bool read(uint8_t reg, uint8_t* data, std::size_t length) override;
    bool write(uint8_t reg, uint8_t value) override;

private:
    Bmi270& host_;
};

} // namespace platform
