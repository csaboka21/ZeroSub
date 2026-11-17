/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>

namespace app {

struct ApplicationConfig {
    bool dark_mode = false;
    /* spidev node of the CC1101, e.g. /dev/spidev0.0. Empty leaves the radio disabled. */
    std::string cc1101_spi_device;
    std::uint32_t cc1101_spi_speed_hz = 500000;
    /* The capture library. Empty selects "zerosub/sub-ghz" on the SD card or in the home directory. */
    std::string capture_directory;
    /* LIRC devices for infrared, e.g. /dev/lirc0. Empty picks the first one that can do the job. */
    std::string infrared_receive_device;
    std::string infrared_send_device;
    /* The remote library. Empty selects "zerosub/Infrared" on the SD card or in the home directory. */
    std::string infrared_directory;
    /* spidev node of the Cap's ST25R3916 NFC reader. Empty disables the NFC tool. */
    std::string nfc_spi_device = "/dev/spidev0.2";
    std::uint32_t nfc_spi_speed_hz = 5000000;
    /* The tag library. Empty selects "zerosub/NFC" on the SD card or in the home directory. */
    std::string nfc_directory;
    /*
     * The 125 kHz front-end on the EXT header: the GPIO that loads the coil
     * (EXT pin 3) and the field-detect input (EXT pin 5). -1 disables the tool,
     * or field detect.
     */
    std::string rfid_gpio_chip = "/dev/gpiochip0";
    int rfid_modulation_gpio = 23;
    int rfid_field_gpio = 22;
    /*
     * Reading: the comparator output (EXT pin 12) and the UART whose TX pin
     * (EXT pin 14) drives the coil at 125 kHz. -1 or an empty UART turns
     * reading off. The relay GPIO is only for the rev B front-end; -1 (the
     * default) means no relay.
     */
    int rfid_relay_gpio = -1;
    int rfid_reader_gpio = 15;
    std::string rfid_carrier_uart = "/dev/serial0";
    /* The key library. Empty selects "zerosub/RFID" on the SD card or in the home directory. */
    std::string rfid_directory;
    /* The BMI270 / BMM150 IMU. An empty bus searches every /dev/i2c-* node. */
    bool imu_enabled = true;
    std::string imu_i2c_device;
};

bool load_application_config(const std::string& path,
                             ApplicationConfig& config,
                             std::string& error);
bool save_application_config(const std::string& path,
                             const ApplicationConfig& config,
                             std::string& error);

} // namespace app
