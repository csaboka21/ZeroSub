/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gpio_line.h"
#include "spi_device.h"

#include <cstdint>
#include <memory>
#include <string>

namespace platform {

struct Cc1101Config {
    std::string spi_device;
    uint32_t spi_speed_hz{500000};
    /* GDO0 line on the Cap CC1101, used as the demodulated data output for capture. */
    int data_line{15};
    /* RF_SW0 antenna-switch line on the Cap CC1101. A negative line leaves it undriven. */
    std::string gpio_chip{"/dev/gpiochip0"};
    int rf_switch_line{14};
    /* Reference crystal of the module. 26 MHz is the usual CC1101 value. */
    uint32_t crystal_hz{26000000};
};

/* Receive modem presets, matching the ones the Flipper Zero Sub-GHz app offers. */
enum class Cc1101Preset {
    Am270 = 0,
    Am650,
    Fm238,
    Fm476,
};

constexpr int kCc1101PresetCount = 4;

const char* cc1101_preset_name(Cc1101Preset preset);
/* The matching preset name used in Flipper Zero .sub files. */
const char* cc1101_preset_sub_file_name(Cc1101Preset preset);

struct Cc1101ChipInfo {
    uint8_t part_number{0};
    uint8_t version{0};
};

/*
 * CC1101 driver over spidev, using the chip's asynchronous serial mode: the
 * demodulated or to-be-sent bit stream is a plain level on the GDO0 pin.
 *
 * Besides SPI it drives one host line, the RF_SW0 antenna switch, and restores
 * that pin's previous function when destroyed. The chip's GDO0 pin only becomes
 * an output while set_data_output(true) is in effect. The Cap must already be
 * powered (see CapPower).
 *
 * The driver does not know about regional limits: callers decide whether a
 * frequency may be transmitted on before calling start_transmit().
 */
class Cc1101 {
public:
    /* Opens the SPI device, resets the chip, and verifies its identity. */
    static std::unique_ptr<Cc1101> open(const Cc1101Config& config, std::string& error);

    ~Cc1101();
    Cc1101(const Cc1101&) = delete;
    Cc1101& operator=(const Cc1101&) = delete;

    const Cc1101ChipInfo& chip_info() const;
    /* False when RF_SW0 could not be taken over, so the antenna path may not match the band. */
    bool antenna_switch_active() const;

    /* Loads the modem settings and leaves the chip idle; call start_receive() afterwards. */
    bool apply_preset(Cc1101Preset preset, std::string& error);
    /*
     * Routes the demodulated bit stream to GDO0 (asynchronous serial mode), or
     * returns the pin to high impedance. Enable it only while the host reads GDO0.
     */
    bool set_data_output(bool enabled, std::string& error);
    /* Tunes to the carrier and enters continuous receive. */
    bool start_receive(uint32_t frequency_hz, std::string& error);
    /*
     * Tunes to the carrier and turns the transmitter on, using the preset last
     * applied. GDO0 becomes the data input: the host keys the carrier by driving
     * that line, and must hold it low until this returns. Call idle() to stop.
     */
    bool start_transmit(uint32_t frequency_hz, std::string& error);
    /* Current channel signal strength; only meaningful while receiving. */
    bool read_rssi_dbm(int32_t& rssi_dbm, std::string& error);
    bool idle(std::string& error);

private:
    Cc1101(std::unique_ptr<spi::Device> device, const Cc1101Config& config);

    bool strobe(uint8_t command, std::string& error);
    bool write_register(uint8_t address, uint8_t value, std::string& error);
    bool read_status_register(uint8_t address, uint8_t& value, std::string& error);
    bool reset_and_identify(std::string& error);
    void claim_antenna_switch();
    bool select_antenna(uint32_t frequency_hz, std::string& error);
    bool tune(uint32_t frequency_hz, std::string& error);

    std::unique_ptr<spi::Device> device_;
    Cc1101Config config_;
    Cc1101ChipInfo chip_info_;
    bool data_output_{false};
    Cc1101Preset preset_{Cc1101Preset::Am650};
    gpio::PinFunctionSnapshot rf_switch_pin_state_;
    std::unique_ptr<gpio::OutputLine> rf_switch_;
};

} // namespace platform
