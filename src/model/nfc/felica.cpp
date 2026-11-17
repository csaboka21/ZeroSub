/*
 * SPDX-License-Identifier: MIT
 */

#include "felica.h"

namespace model {

/*
 * FeliCa Lite memory map: 14 S_PAD blocks, REG, then the system blocks RC,
 * MAC, ID, D_ID, SER_C, SYS_C, CKV, CK, MC, and Lite-S's WCNT, MAC_A,
 * STATE and CRC_CHECK.
 */
const std::array<uint8_t, kFelicaLiteBlocks> kFelicaLiteBlockNumbers = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
    0x0E, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x90, 0x91, 0x92, 0xA0,
};

uint16_t felica_crc(const uint8_t* bytes, std::size_t size) {
    uint16_t crc = 0x0000;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= static_cast<uint16_t>(bytes[index] << 8);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) != 0 ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

bool felica_is_lite(const std::array<uint8_t, 8>& pmm) {
    /* IC type F0 is Lite; F1, and F2 (FeliCa Link in Lite-S mode), Lite-S. */
    return pmm[1] == 0xF0 || pmm[1] == 0xF1 || pmm[1] == 0xF2;
}

std::string felica_ic_name(const std::array<uint8_t, 8>& pmm) {
    switch (pmm[1]) {
        case 0xF0:
            return "FeliCa Lite";
        case 0xF1:
            return "FeliCa Lite-S";
        case 0xF2:
            return "FeliCa Link (Lite-S mode)";
        case 0xE0:
            return "FeliCa Plug";
        case 0xE1:
            return "FeliCa Link";
        default:
            break;
    }
    /* Low IC types are the Standard chips and the phones' mobile FeliCa (06-07, 10-1F). */
    if (pmm[1] == 0x06 || pmm[1] == 0x07 || (pmm[1] >= 0x10 && pmm[1] <= 0x1F)) {
        return "Mobile FeliCa";
    }
    if (pmm[1] <= 0x48 || pmm[1] == 0xA2) {
        return "FeliCa Standard";
    }
    return "FeliCa";
}

} // namespace model
