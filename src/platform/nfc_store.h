/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nfc/nfc_tag.h"

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

/*
 * The tag library when no directory is configured: "zerosub/NFC" on an
 * external SD card if one is mounted, otherwise under the home directory of
 * the user who started the app.
 */
std::string default_nfc_directory();

/*
 * Reads a Flipper Zero .nfc file. NTAG / Ultralight files are read in full;
 * of any other NFC-A card type only UID, ATQA and SAK are taken. Fails for
 * files that are not NFC-A at all.
 */
bool load_nfc_file(const std::string& path, model::NfcTag& tag, std::string& error);

/*
 * Writes the tag as a new Flipper Zero .nfc file named after its type and
 * UID, e.g. "NTAG215_0485928AA06181.nfc", with "_2" and so on appended when
 * that name is taken. The directory is created if needed. Returns the full
 * path in `path`.
 */
bool save_new_nfc_file(const std::string& directory,
                       const model::NfcTag& tag,
                       std::string& path,
                       std::string& error);

/* One-line description of a saved tag, e.g. "NTAG215  04 85 92 8A A0 61 81". */
std::string describe_nfc_file(const std::string& path);

/* Permanently deletes one .nfc file. */
bool delete_nfc_file(const std::string& path, std::string& error);

/*
 * The owner's amiibo retail key, as amiibo_sim names it: key_retail.bin, or
 * else retail.bin, in the library's top folder. Only the 160-byte file is
 * read; it is never copied or written anywhere.
 */
bool load_amiibo_key_file(const std::string& directory, std::vector<uint8_t>& raw, std::string& error);

} // namespace platform
