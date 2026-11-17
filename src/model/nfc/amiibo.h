/*
 * SPDX-License-Identifier: MIT
 *
 * Amiibo data on an NTAG215: checking it against the owner's retail keys and
 * giving the same amiibo a new UID, re-encrypted and re-signed so a console
 * accepts it. The data layout and the key derivation follow amiitool (MIT,
 * Copyright (c) 2015-2017 Marcos Del Sol Vives, (c) 2016 javiMaD, (c) 2016
 * Michael Armbruster, (c) 2020-2021 nitz) as used by amiibo_sim. No keys are
 * part of this code: they come from the owner's key_retail.bin.
 *
 * Pure logic, no hardware or file access.
 */

#pragma once

#include "nfc_tag.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace model {

/* An amiibo is an NTAG215: 135 pages, 540 bytes. */
constexpr std::size_t kAmiiboDumpSize = 540;
/* key_retail.bin: two 80-byte master keys, data first, then tag. */
constexpr std::size_t kAmiiboKeyFileSize = 160;

struct AmiiboMasterKey {
    std::array<uint8_t, 16> hmac_key{};
    /* Including its terminating NUL. */
    std::vector<uint8_t> type_string;
    std::vector<uint8_t> magic_bytes;
    std::array<uint8_t, 32> xor_pad{};
};

struct AmiiboKeys {
    AmiiboMasterKey data;
    AmiiboMasterKey tag;
};

/* Parses the contents of key_retail.bin. */
bool amiibo_parse_keys(const std::vector<uint8_t>& raw, AmiiboKeys& keys, std::string& error);

/* An NTAG215 with all 540 bytes and a 7-byte UID: something that can be an amiibo. */
bool amiibo_shaped(const NfcTag& tag);

/* True when the tag's amiibo data is signed with these keys. */
bool amiibo_verify(const NfcTag& tag, const AmiiboKeys& keys, std::string& error);

/*
 * The same amiibo with `uid` (7 bytes, NXP: first byte 0x04) and a fresh
 * 32-byte `salt`: the UID pages, the password and both signatures follow.
 * The original must verify with `keys`.
 */
bool amiibo_with_uid(const NfcTag& tag,
                     const AmiiboKeys& keys,
                     const std::array<uint8_t, 7>& uid,
                     const std::array<uint8_t, 32>& salt,
                     NfcTag& out,
                     std::string& error);

} // namespace model
