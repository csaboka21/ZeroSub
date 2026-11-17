/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "rfid/em4100.h"

#include <string>

namespace platform {

/*
 * The 125 kHz key library when no directory is configured: "zerosub/RFID" on
 * an external SD card if one is mounted, otherwise under the home directory
 * of the user who started the app.
 */
std::string default_rfid_directory();

/* Reads a Flipper Zero .rfid file ("Flipper RFID key"), of any key type. */
bool load_rfid_file(const std::string& path, model::RfidKey& key, std::string& error);

/*
 * Writes the key as a new .rfid file named after its type and data, e.g.
 * "EM4100_0123456789.rfid", with "_2" and so on appended when that name is
 * taken. The directory is created if needed. Returns the full path in `path`.
 */
bool save_new_rfid_file(const std::string& directory,
                        const model::RfidKey& key,
                        std::string& path,
                        std::string& error);

/* One-line description of a saved key, e.g. "EM4100  01 23 45 67 89". */
std::string describe_rfid_file(const std::string& path);

/* Permanently deletes one .rfid file. */
bool delete_rfid_file(const std::string& path, std::string& error);

} // namespace platform
