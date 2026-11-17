/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>

namespace model {

/*
 * Where the app is allowed to transmit. Receiving is never restricted.
 *
 * The limits are the European licence-exempt short-range device bands that
 * remotes use:
 *
 *   433.050 - 434.790 MHz
 *   868.000 - 868.600 MHz
 *
 * Every transmission goes through transmit_allowed() immediately before the
 * carrier is turned on. The bands are fixed in code; they are not a setting.
 *
 * The check covers the carrier frequency only. Duty-cycle limits (10% in the
 * 433 MHz band, 1% in 868.0 - 868.6 MHz) are not enforced and remain the
 * user's responsibility.
 */
bool transmit_allowed(uint32_t frequency_hz, std::string& reason);

/* Short description of the allowed bands, for messages and help text. */
const char* transmit_bands_text();

} // namespace model
