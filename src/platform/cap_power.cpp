/*
 * SPDX-License-Identifier: MIT
 */

#include "cap_power.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <string>

namespace platform {
namespace {

constexpr const char* kGpioChip = "/dev/gpiochip0";
constexpr unsigned int kPowerEnableLine = 26;
constexpr const char* kExt5vBrightness = "/sys/class/leds/ext_5v_out/brightness";

} // namespace

bool CapPower::enable(std::string& error) {
    error.clear();
    if (enabled_) {
        return true;
    }

    if (!power_enable_) {
        power_enable_ = gpio::OutputLine::request(kGpioChip, kPowerEnableLine, true, error);
        if (!power_enable_) {
            error = "Cap power: " + error;
            return false;
        }
    }

    /* Only write the rail control when it is off, so an already-powered Cap needs no extra rights. */
    int brightness = -1;
    {
        std::ifstream input(kExt5vBrightness);
        if (!input) {
            /* No LED-class rail control on this system image; GPIO26 alone may be enough. */
            enabled_ = true;
            return true;
        }
        input >> brightness;
    }

    if (brightness <= 0) {
        std::ofstream output(kExt5vBrightness);
        output << '1';
        output.flush();
        if (!output) {
            error = std::string("Cap power: cannot enable ext_5v_out (") + std::strerror(errno) +
                    "); the gpio group needs write access (see /usr/lib/tmpfiles.d/zerosub.conf)";
            return false;
        }
    }

    enabled_ = true;
    return true;
}

bool CapPower::enabled() const {
    return enabled_;
}

} // namespace platform
