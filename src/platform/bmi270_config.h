/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace platform {

/*
 * The 8 KB feature-engine image the BMI270 needs before its accelerometer
 * and gyroscope work. Bosch Sensortec, BSD-3-Clause; see bmi270_config.cpp.
 */
extern const std::uint8_t kBmi270ConfigFile[];
extern const std::size_t kBmi270ConfigFileSize;

} // namespace platform
