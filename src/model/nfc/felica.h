/*
 * SPDX-License-Identifier: MIT
 *
 * FeliCa (NFC-F) as the NFC tool reads it: the card's IDm and PMm, the
 * memory of a FeliCa Lite / Lite-S, and for FeliCa Standard the systems,
 * areas and services it lists and the blocks of its public (readable
 * without a key) services. Files use Flipper Zero's FeliCa format.
 *
 * Pure logic, no hardware access.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace model {

constexpr std::size_t kFelicaBlockSize = 16;
/* A FeliCa Lite dump: these 28 blocks, each kept with its two status flags (18 bytes). */
constexpr std::size_t kFelicaLiteBlocks = 28;
constexpr std::size_t kFelicaLiteRecord = 2 + kFelicaBlockSize;
/* The block numbers of a FeliCa Lite dump, in file order. */
extern const std::array<uint8_t, kFelicaLiteBlocks> kFelicaLiteBlockNumbers;

struct FelicaArea {
    uint16_t code{0};
    uint16_t end{0};
    /* The services of this area, as indexes into the system's service list. */
    uint16_t first{0};
    uint16_t last{0};
};

struct FelicaPublicBlock {
    uint16_t service{0};
    uint8_t index{0};
    std::array<uint8_t, kFelicaBlockSize> data{};
};

struct FelicaSystem {
    uint16_t code{0};
    std::vector<FelicaArea> areas;
    /* Service codes; the low six bits are the attributes. */
    std::vector<uint16_t> services;
    std::vector<FelicaPublicBlock> blocks;
};

/* Service attribute bit 0: readable without authentication. */
inline bool felica_service_public(uint16_t service) {
    return (service & 0x0001) != 0;
}

/* FeliCa's CRC (CCITT, start 0000, most significant bit first); sent high byte first. */
uint16_t felica_crc(const uint8_t* bytes, std::size_t size);

/* Lite / Lite-S, told by the IC type in PMm. */
bool felica_is_lite(const std::array<uint8_t, 8>& pmm);
/* "FeliCa Lite-S", "FeliCa Standard", ... from PMm. */
std::string felica_ic_name(const std::array<uint8_t, 8>& pmm);

} // namespace model
