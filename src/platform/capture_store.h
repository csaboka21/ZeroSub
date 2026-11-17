/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct RawCapture {
    uint32_t frequency_hz{0};
    /* Flipper preset name, e.g. "FuriHalSubGhzPresetOok650Async". */
    std::string preset;
    /* Signed durations in microseconds: positive = carrier on, negative = carrier off. */
    std::vector<int32_t> samples;
};

/* A decoded fixed-code key, as stored in a Flipper "SubGhz Key File". */
struct KeyCapture {
    uint32_t frequency_hz{0};
    std::string preset;
    /* Flipper protocol name, e.g. "Princeton". */
    std::string protocol;
    uint32_t bits{0};
    uint64_t key{0};
    uint32_t te_us{0};
    /* Bits of the frame that follow the key, for the few protocols with frames over 64 bits. */
    uint64_t extra{0};
    uint32_t extra_bits{0};
};

/* One entry of the capture library: a .sub file or a folder. */
struct CaptureFile {
    std::string path;
    /* File name without ".sub", or the folder name. */
    std::string name;
    /* Filled on demand with describe_capture(); empty until then. */
    std::string summary;
    bool is_directory{false};
};

/* Running totals over a pulse train of signed microsecond durations. */
struct PulseStats {
    std::size_t count{0};
    uint64_t duration_us{0};
    /* Shortest carrier-on or carrier-off period; 0 while empty. */
    uint32_t shortest_us{0};

    void add(int32_t duration_us_signed);
};

/* Everything the summary page shows about one saved .sub file. */
struct CaptureDetails {
    /* True for a RAW recording, false for a decoded key. */
    bool raw{false};
    uint32_t frequency_hz{0};
    /* Flipper preset name as written in the file. */
    std::string preset;
    std::string protocol;
    /* Key files only. te_us is 0 when the file has no TE line. */
    uint32_t bits{0};
    uint64_t key{0};
    uint32_t te_us{0};
    /* Bits that follow the key in a long frame; extra_bits is 0 when the file has none. */
    uint64_t extra{0};
    uint32_t extra_bits{0};
    /* RAW files only. */
    PulseStats pulses;
};

/*
 * Mount point of an inserted SD card or USB drive, or empty when there is none.
 * Only file systems mounted under /media, /run/media or /mnt count, so the
 * system's own partitions are never picked.
 */
std::string removable_storage_mount();

/*
 * The capture library when no directory is configured: "zerosub/sub-ghz" on an
 * external SD card if one is mounted, otherwise "zerosub/sub-ghz" under the home
 * directory of the user who started the app (the invoking user's home when
 * started through sudo). "zerosub" is the app's data folder; each radio module
 * gets its own folder inside it.
 */
std::string default_capture_directory();

/*
 * The same choice for another module's folder inside "zerosub", e.g.
 * "Infrared": on the SD card if one is mounted, otherwise in the home directory.
 */
std::string default_module_directory(const char* module_folder);

/*
 * The home directory of the user who started the app: the invoking user's
 * when started through sudo, otherwise $HOME (%USERPROFILE% on Windows).
 */
std::string user_home_directory();

/* When running under sudo, makes the invoking user the owner of a file or folder the app created. */
void hand_to_invoking_user(const std::string& path);

/* Creates the library folder if it is missing, owned by the invoking user. */
bool ensure_capture_directory(const std::string& directory, std::string& error);

/*
 * Writes the capture as a Flipper Zero "SubGhz RAW File" (.sub) with a
 * timestamped name, creating the directory if needed. When running under sudo
 * the file is handed to the invoking user. Returns the full path in `path`.
 */
bool save_raw_capture(const std::string& directory,
                      const RawCapture& capture,
                      std::string& path,
                      std::string& error);

/* Writes a decoded key as a Flipper Zero "SubGhz Key File", named after the protocol and key. */
bool save_key_capture(const std::string& directory,
                      const KeyCapture& capture,
                      std::string& path,
                      std::string& error);

/*
 * Lists one folder of the library: sub-folders first, then .sub files, each
 * group in alphabetical order. Hidden entries are skipped. A missing directory
 * is an empty list.
 */
bool list_directory(const std::string& directory,
                    std::vector<CaptureFile>& entries,
                    std::string& error);

/* The same listing for files with another extension, e.g. ".ir". */
bool list_directory(const std::string& directory,
                    const char* extension,
                    std::vector<CaptureFile>& entries,
                    std::string& error);

/* One-line description from a .sub file's header, e.g. "Princeton 24 bit  433.92 MHz". */
std::string describe_capture(const std::string& path);

/* Reads one .sub file in full. Fails when it is not a Sub-GHz key or RAW file. */
bool load_capture_details(const std::string& path, CaptureDetails& details, std::string& error);

/*
 * Reads the pulse train of a RAW .sub file: signed microsecond durations,
 * positive for carrier on. Fails for key files and for recordings longer than
 * `max_pulses`.
 */
bool load_raw_samples(const std::string& path,
                      std::size_t max_pulses,
                      std::vector<int32_t>& samples,
                      std::string& error);

/* Permanently deletes one capture file. */
bool delete_capture(const std::string& path, std::string& error);

} // namespace platform
