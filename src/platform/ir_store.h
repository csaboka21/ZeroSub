/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace platform {

/*
 * One button of a remote, as stored in a Flipper Zero .ir file: either a
 * decoded message (protocol, address, command) or raw timings with the
 * carrier they were sent on.
 */
struct IrSignal {
    std::string name;
    bool raw{false};
    /* Decoded signals: the Flipper protocol name, e.g. "NECext". */
    std::string protocol;
    uint32_t address{0};
    uint32_t command{0};
    /* Raw signals: microsecond durations that start with a mark and alternate. */
    uint32_t frequency_hz{38000};
    float duty_cycle{0.33F};
    std::vector<uint32_t> timings;
};

/* Most timings a raw signal may hold, as on the Flipper Zero. */
constexpr std::size_t kIrMaxTimings = 1024;

/*
 * The remote library when no directory is configured: "zerosub/Infrared" on an
 * external SD card if one is mounted, otherwise under the home directory of
 * the user who started the app.
 */
std::string default_infrared_directory();

/*
 * Reads every signal of an .ir file. Both Flipper file types are accepted:
 * "IR signals file" (a remote) and "IR library file" (a universal remote).
 * Signals that cannot be read are skipped; fails only when the file is not an
 * .ir file at all.
 */
bool load_ir_file(const std::string& path, std::vector<IrSignal>& signals, std::string& error);

/* Rewrites an .ir file with these signals. The file is replaced in one step. */
bool save_ir_file(const std::string& path, const std::vector<IrSignal>& signals, std::string& error);

/*
 * Writes the signals as a new remote, "<base_name>.ir", or "<base_name>_2.ir"
 * and so on when that name is taken. The directory is created if needed.
 * Returns the full path in `path`.
 */
bool save_new_ir_file(const std::string& directory,
                      const std::string& base_name,
                      const std::vector<IrSignal>& signals,
                      std::string& path,
                      std::string& error);

/* One-line description of a remote, e.g. "3 buttons: Power, Vol_up, Vol_dn". */
std::string describe_ir_file(const std::string& path);

/* Permanently deletes one .ir file. */
bool delete_ir_file(const std::string& path, std::string& error);

/* "RAW_20260131_101500": a name for something saved now. */
std::string ir_timestamped_name(const char* prefix);

} // namespace platform
