/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace model {

/*
 * Rolling-code remotes send a different code on every press, and the receiver
 * only accepts codes newer than the last one it saw. The app recognises their
 * frames so it can show them and refuse to send them: it never transmits a
 * captured rolling code, as a key or inside a RAW recording.
 */

/* True for the protocol names (as used in .sub files) that are rolling codes. */
bool is_rolling_protocol(const std::string& protocol);

/* The part of a rolling-code frame that identifies the remote and stays the same between presses. */
struct RollingIdentity {
    uint32_t serial{0};
    uint32_t button{0};
};

/*
 * Reads the serial number and button from a frame, for the protocols that send
 * them unencrypted (KeeLoq, Star Line, Somfy). Returns false for the others.
 */
bool rolling_identity(const std::string& protocol, uint64_t key, RollingIdentity& identity);

/*
 * Looks for a rolling-code frame in a pulse train of signed microsecond
 * durations. Returns true and the protocol name of the first one found. Only
 * protocols the decoders know can be found.
 */
bool find_rolling_code(const std::vector<int32_t>& pulses, std::string& protocol);

} // namespace model
