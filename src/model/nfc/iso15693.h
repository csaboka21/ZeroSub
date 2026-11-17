/*
 * SPDX-License-Identifier: MIT
 *
 * NFC-V (ISO/IEC 15693) as the ST25R3916 handles it: the chip has no NFC-V
 * framing of its own, so requests are encoded here (1-out-of-4 pulse
 * position, as FIFO bits in subcarrier stream mode) and answers decoded
 * here (Manchester). The coding follows M5Stack's M5Unit-NFC library (MIT,
 * Copyright (c) 2025 M5Stack Technology CO LTD).
 *
 * Pure logic, no hardware access.
 */

#pragma once

#include "nfc_tag.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace model {

/* Request flags: high data rate, and either inventory with one slot or addressed. */
constexpr uint8_t kIso15693FlagsInventory = 0x26;
constexpr uint8_t kIso15693FlagsAddressed = 0x22;
/* Addressed, and the option flag: READ SINGLE BLOCK then also returns the block's lock status. */
constexpr uint8_t kIso15693FlagsAddressedOption = 0x62;
/* Answer flags: bit 0 set means an error code follows. */
constexpr uint8_t kIso15693ResponseError = 0x01;

constexpr uint8_t kIso15693CmdInventory = 0x01;
constexpr uint8_t kIso15693CmdReadSingleBlock = 0x20;
constexpr uint8_t kIso15693CmdGetSystemInfo = 0x2B;

/* ISO/IEC 13239 CRC as ISO 15693 uses it, already inverted; sent low byte first. */
uint16_t iso15693_crc(const uint8_t* bytes, std::size_t size);

/* A request (flags, command, parameters) with its CRC, coded as the bytes the chip transmits. */
std::vector<uint8_t> iso15693_encode_request(const std::vector<uint8_t>& request);

enum class Iso15693Decode {
    Ok,
    /* Several tags answered at once. */
    Collision,
    /* Damaged, too short, or a wrong CRC. */
    Error,
};

/* Decodes what the chip received into the tag's answer, without its CRC. */
Iso15693Decode iso15693_decode_response(const uint8_t* fifo, std::size_t size, std::vector<uint8_t>& answer);

/* The UID as printed (E0 first) from the order it travels in (E0 last), and back. */
std::vector<uint8_t> iso15693_uid_from_air(const uint8_t* air);
std::vector<uint8_t> iso15693_uid_to_air(const std::vector<uint8_t>& uid);

/* "NXP ICODE SLIX", "TI Tag-it HF-I", "ISO 15693 (STMicroelectronics)", ... from the UID. */
std::string iso15693_type_name(const std::vector<uint8_t>& uid);

} // namespace model
