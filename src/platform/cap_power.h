/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gpio_line.h"

#include <memory>
#include <string>

namespace platform {

/*
 * Powers the CardputerZero Cap connector: POWER_EN on GPIO26 plus the EXT 5V
 * rail exposed as the `ext_5v_out` LED-class device.
 *
 * The sequence follows M5Stack's Cap-CC1101-SubG-Chat app. Like that app, this
 * never switches the supply off again: the Cap shares its SPI bus with the LCD,
 * and cutting the Cap's I/O supply can freeze the display.
 */
class CapPower {
public:
    /* Time the Cap needs after enable() before the radio answers on SPI. */
    static constexpr unsigned int kSettleMs = 1500;

    /* Idempotent. Returns false with an error if the supply could not be enabled. */
    bool enable(std::string& error);
    bool enabled() const;

private:
    std::unique_ptr<gpio::OutputLine> power_enable_;
    bool enabled_{false};
};

} // namespace platform
