/*
 * SPDX-License-Identifier: MIT
 */

#include "capture_store.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <system_error>
#include <utility>

#if !defined(_WIN32)
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace platform {
namespace {

/* The Flipper reads at most this many values per RAW_Data line. */
constexpr std::size_t kValuesPerLine = 512;

#if !defined(_WIN32)
/* The user who ran sudo, if any. */
bool invoking_user(uid_t& uid, gid_t& gid) {
    const char* uid_text = std::getenv("SUDO_UID");
    const char* gid_text = std::getenv("SUDO_GID");
    if (!uid_text || !gid_text || ::geteuid() != 0) {
        return false;
    }

    char* end = nullptr;
    const unsigned long parsed_uid = std::strtoul(uid_text, &end, 10);
    if (end == uid_text || *end != '\0') {
        return false;
    }
    const unsigned long parsed_gid = std::strtoul(gid_text, &end, 10);
    if (end == gid_text || *end != '\0') {
        return false;
    }
    uid = static_cast<uid_t>(parsed_uid);
    gid = static_cast<gid_t>(parsed_gid);
    return true;
}
#endif

void hand_to_invoking_user_path(const std::filesystem::path& path) {
#if !defined(_WIN32)
    uid_t uid = 0;
    gid_t gid = 0;
    if (invoking_user(uid, gid)) {
        /* Best effort: the capture is still usable if this fails. */
        const int ignored = ::chown(path.c_str(), uid, gid);
        (void)ignored;
    }
#else
    (void)path;
#endif
}

std::string timestamped_name() {
    const std::time_t now = std::time(nullptr);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &now);
#else
    localtime_r(&now, &local_time);
#endif
    char text[40];
    std::strftime(text, sizeof(text), "RAW_%Y%m%d_%H%M%S", &local_time);
    return text;
}

bool prepare_directory(const std::string& directory, std::string& error) {
    const std::filesystem::path root(directory);
    std::error_code filesystem_error;

    /* Remember which levels are missing, so each new one can be handed to the invoking user. */
    std::vector<std::filesystem::path> missing;
    for (std::filesystem::path level = root;
         !level.empty() && !std::filesystem::exists(level, filesystem_error);
         level = level.parent_path()) {
        missing.push_back(level);
        if (level == level.root_path()) {
            break;
        }
    }

    std::filesystem::create_directories(root, filesystem_error);
    if (filesystem_error) {
        error = "cannot create " + directory + ": " + filesystem_error.message();
        return false;
    }
    for (const auto& level : missing) {
        hand_to_invoking_user_path(level);
    }
    return true;
}

/* All ZeroSub data lives under one folder, with one sub-folder per radio module. */
constexpr const char* kDataFolder = "zerosub";
constexpr const char* kSubGhzFolder = "sub-ghz";

/* First unused "<base>.sub", "<base>_2.sub", ... in the directory. */
std::filesystem::path unused_path(const std::filesystem::path& root, const std::string& base) {
    std::error_code filesystem_error;
    std::filesystem::path file = root / (base + ".sub");
    for (int suffix = 2; std::filesystem::exists(file, filesystem_error) && suffix < 100; ++suffix) {
        file = root / (base + "_" + std::to_string(suffix) + ".sub");
    }
    return file;
}

std::string header_value(const std::string& line, const char* field) {
    const std::string prefix = std::string(field) + ": ";
    return line.compare(0, prefix.size(), prefix) == 0 ? line.substr(prefix.size()) : std::string();
}

/*
 * Protocols whose frame is longer than the 64-bit key. The bits after the key
 * go into their own field, named and formatted as the Flipper Zero does, so
 * the file holds the whole frame.
 */
struct ExtraField {
    const char* protocol;
    /* Frame length this applies to; 0 for any. */
    uint32_t frame_bits;
    const char* field;
    uint32_t extra_bits;
    /* Eight hex bytes instead of a decimal number. */
    bool hex_bytes;
};

constexpr ExtraField kExtraFields[] = {
    {"Nice FloR-S", 72, "Data", 20, false},
    {"Somfy Keytis", 0, "Duration_Counter", 24, false},
    {"Alutech AT-4N", 0, "CRC", 8, false},
    {"KingGates Stylo4k", 0, "Data", 36, true},
};

const ExtraField* extra_field_for(const std::string& protocol, uint32_t bits) {
    for (const auto& field : kExtraFields) {
        if (protocol == field.protocol && (field.frame_bits == 0 || field.frame_bits == bits)) {
            return &field;
        }
    }
    return nullptr;
}

/* Builds the one-line description of a .sub file from its header. */
std::string summarize(const std::filesystem::path& file) {
    std::ifstream input(file);
    std::string protocol;
    std::string bits;
    std::string frequency;
    std::string line;
    for (int count = 0; count < 12 && std::getline(input, line); ++count) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.compare(0, 9, "RAW_Data:") == 0) {
            break;
        }
        if (auto value = header_value(line, "Protocol"); !value.empty()) {
            protocol = value;
        }
        else if (value = header_value(line, "Bit"); !value.empty()) {
            bits = value;
        }
        else if (value = header_value(line, "Frequency"); !value.empty()) {
            frequency = value;
        }
    }
    if (protocol.empty()) {
        return "Not a Sub-GHz file";
    }

    std::string summary = protocol;
    if (!bits.empty()) {
        summary += " " + bits + " bit";
    }
    char* end = nullptr;
    const unsigned long hz = std::strtoul(frequency.c_str(), &end, 10);
    if (end != frequency.c_str() && hz > 0) {
        char text[32];
        std::snprintf(text, sizeof(text), "  %lu.%02lu MHz", hz / 1000000UL, (hz % 1000000UL) / 10000UL);
        summary += text;
    }
    return summary;
}

} // namespace

std::string removable_storage_mount() {
#if defined(__linux__)
    std::ifstream mounts("/proc/mounts");
    std::string device;
    std::string mount_point;
    std::string rest;
    while (mounts >> device >> mount_point && std::getline(mounts, rest)) {
        /* /proc/mounts writes a space in a path as "\040". */
        std::string decoded;
        for (std::size_t index = 0; index < mount_point.size(); ++index) {
            if (mount_point.compare(index, 4, "\\040") == 0) {
                decoded += ' ';
                index += 3;
            }
            else {
                decoded += mount_point[index];
            }
        }

        const bool block_device = device.compare(0, 5, "/dev/") == 0;
        const bool user_mount = decoded.compare(0, 7, "/media/") == 0 ||
                                decoded.compare(0, 11, "/run/media/") == 0 ||
                                decoded.compare(0, 5, "/mnt/") == 0;
        /* The rest of the line is " <type> <options> ...". Captures are written here, so skip read-only mounts. */
        const std::size_t type_start = rest.find_first_not_of(' ');
        const std::size_t options_start =
            type_start == std::string::npos ? std::string::npos : rest.find(' ', type_start);
        const bool writable = options_start != std::string::npos &&
                              rest.compare(options_start + 1, 2, "rw") == 0;
        if (block_device && user_mount && writable) {
            return decoded;
        }
    }
#endif
    return std::string();
}

std::string default_capture_directory() {
    return default_module_directory(kSubGhzFolder);
}

void hand_to_invoking_user(const std::string& path) {
    hand_to_invoking_user_path(path);
}

std::string user_home_directory() {
    std::filesystem::path home;
#if !defined(_WIN32)
    uid_t uid = 0;
    gid_t gid = 0;
    if (invoking_user(uid, gid)) {
        if (const passwd* entry = ::getpwuid(uid)) {
            if (entry->pw_dir && entry->pw_dir[0] != ' ') {
                home = entry->pw_dir;
            }
        }
    }
    if (home.empty()) {
        if (const char* value = std::getenv("HOME")) {
            home = value;
        }
    }
#else
    if (const char* value = std::getenv("USERPROFILE")) {
        home = value;
    }
#endif
    if (home.empty()) {
        home = ".";
    }
    return home.string();
}

std::string default_module_directory(const char* module_folder) {
    const std::string card = removable_storage_mount();
    if (!card.empty()) {
        return (std::filesystem::path(card) / kDataFolder / module_folder).string();
    }
    return (std::filesystem::path(user_home_directory()) / kDataFolder / module_folder).string();
}

bool ensure_capture_directory(const std::string& directory, std::string& error) {
    error.clear();
    return prepare_directory(directory, error);
}

bool save_raw_capture(const std::string& directory,
                      const RawCapture& capture,
                      std::string& path,
                      std::string& error) {
    error.clear();
    path.clear();

    /* A RAW file starts with a carrier-on period, so skip a leading silence. */
    std::size_t first = 0;
    while (first < capture.samples.size() && capture.samples[first] <= 0) {
        ++first;
    }
    if (first >= capture.samples.size()) {
        error = "nothing to save";
        return false;
    }

    if (!prepare_directory(directory, error)) {
        return false;
    }
    /* Two saves in the same second get distinct names. */
    const std::filesystem::path file = unused_path(directory, timestamped_name());

    std::ofstream output(file, std::ios::trunc);
    if (!output) {
        error = "cannot write " + file.string();
        return false;
    }

    output << "Filetype: Flipper SubGhz RAW File\n"
           << "Version: 1\n"
           << "Frequency: " << capture.frequency_hz << "\n"
           << "Preset: " << capture.preset << "\n"
           << "Protocol: RAW\n";
    for (std::size_t index = first; index < capture.samples.size(); ++index) {
        const std::size_t column = (index - first) % kValuesPerLine;
        if (column == 0) {
            output << (index == first ? "" : "\n") << "RAW_Data:";
        }
        output << ' ' << capture.samples[index];
    }
    output << '\n';
    output.flush();
    if (!output) {
        error = "failed while writing " + file.string();
        return false;
    }
    output.close();

    hand_to_invoking_user_path(file);
    path = file.string();
    return true;
}

bool save_key_capture(const std::string& directory,
                      const KeyCapture& capture,
                      std::string& path,
                      std::string& error) {
    error.clear();
    path.clear();
    if (capture.protocol.empty() || capture.bits == 0 || capture.bits > 64 + capture.extra_bits) {
        error = "nothing to save";
        return false;
    }
    if (!prepare_directory(directory, error)) {
        return false;
    }

    /* File name: protocol and key, e.g. "Nice_FLO_5C9.sub". */
    std::string base;
    for (const char character : capture.protocol) {
        const bool plain = (character >= '0' && character <= '9') ||
                           (character >= 'A' && character <= 'Z') ||
                           (character >= 'a' && character <= 'z');
        base += plain ? character : '_';
    }
    char key_text[24];
    std::snprintf(key_text, sizeof(key_text), "_%llX", static_cast<unsigned long long>(capture.key));
    const std::filesystem::path file = unused_path(directory, base + key_text);

    std::ofstream output(file, std::ios::trunc);
    if (!output) {
        error = "cannot write " + file.string();
        return false;
    }

    /* The key is written as eight bytes, most significant first. */
    char key_bytes[32];
    std::snprintf(key_bytes,
                  sizeof(key_bytes),
                  "%02X %02X %02X %02X %02X %02X %02X %02X",
                  static_cast<unsigned>((capture.key >> 56) & 0xFF),
                  static_cast<unsigned>((capture.key >> 48) & 0xFF),
                  static_cast<unsigned>((capture.key >> 40) & 0xFF),
                  static_cast<unsigned>((capture.key >> 32) & 0xFF),
                  static_cast<unsigned>((capture.key >> 24) & 0xFF),
                  static_cast<unsigned>((capture.key >> 16) & 0xFF),
                  static_cast<unsigned>((capture.key >> 8) & 0xFF),
                  static_cast<unsigned>(capture.key & 0xFF));
    output << "Filetype: Flipper SubGhz Key File\n"
           << "Version: 1\n"
           << "Frequency: " << capture.frequency_hz << "\n"
           << "Preset: " << capture.preset << "\n"
           << "Protocol: " << capture.protocol << "\n"
           << "Bit: " << capture.bits << "\n"
           << "Key: " << key_bytes << "\n";
    if (capture.protocol == "Princeton" || capture.protocol == "SMC5326") {
        /* These remotes vary in speed, so the measured pulse length is part of the key. */
        output << "TE: " << capture.te_us << "\n";
    }
    if (const ExtraField* field = extra_field_for(capture.protocol, capture.bits)) {
        if (field->hex_bytes) {
            char extra_bytes[32];
            std::snprintf(extra_bytes,
                          sizeof(extra_bytes),
                          "%02X %02X %02X %02X %02X %02X %02X %02X",
                          static_cast<unsigned>((capture.extra >> 56) & 0xFF),
                          static_cast<unsigned>((capture.extra >> 48) & 0xFF),
                          static_cast<unsigned>((capture.extra >> 40) & 0xFF),
                          static_cast<unsigned>((capture.extra >> 32) & 0xFF),
                          static_cast<unsigned>((capture.extra >> 24) & 0xFF),
                          static_cast<unsigned>((capture.extra >> 16) & 0xFF),
                          static_cast<unsigned>((capture.extra >> 8) & 0xFF),
                          static_cast<unsigned>(capture.extra & 0xFF));
            output << field->field << ": " << extra_bytes << "\n";
        }
        else {
            output << field->field << ": " << static_cast<unsigned long>(capture.extra & 0xFFFFFFFFULL) << "\n";
        }
    }
    output.flush();
    if (!output) {
        error = "failed while writing " + file.string();
        return false;
    }
    output.close();

    hand_to_invoking_user_path(file);
    path = file.string();
    return true;
}

bool list_directory(const std::string& directory,
                    std::vector<CaptureFile>& entries,
                    std::string& error) {
    return list_directory(directory, ".sub", entries, error);
}

bool list_directory(const std::string& directory,
                    const char* extension,
                    std::vector<CaptureFile>& entries,
                    std::string& error) {
    error.clear();
    entries.clear();

    std::error_code filesystem_error;
    const std::filesystem::path root(directory);
    if (!std::filesystem::is_directory(root, filesystem_error)) {
        return true;
    }

    for (std::filesystem::directory_iterator entry(root, filesystem_error), end;
         !filesystem_error && entry != end;
         entry.increment(filesystem_error)) {
        const std::string file_name = entry->path().filename().string();
        if (file_name.empty() || file_name.front() == '.') {
            continue;
        }

        std::error_code type_error;
        if (entry->is_directory(type_error)) {
            entries.push_back({entry->path().string(), file_name, std::string(), true});
        }
        else if (entry->path().extension() == extension && entry->is_regular_file(type_error)) {
            entries.push_back({entry->path().string(), entry->path().stem().string(), std::string(), false});
        }
    }
    if (filesystem_error) {
        error = "cannot read " + directory + ": " + filesystem_error.message();
        entries.clear();
        return false;
    }

    const auto lower = [](const std::string& text) {
        std::string result = text;
        for (char& character : result) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
        return result;
    };
    std::sort(entries.begin(), entries.end(), [&lower](const CaptureFile& left, const CaptureFile& right) {
        if (left.is_directory != right.is_directory) {
            return left.is_directory;
        }
        const std::string left_key = lower(left.name);
        const std::string right_key = lower(right.name);
        return left_key != right_key ? left_key < right_key : left.name < right.name;
    });
    return true;
}

std::string describe_capture(const std::string& path) {
    return summarize(path);
}

void PulseStats::add(int32_t duration_us_signed) {
    if (duration_us_signed == 0) {
        return;
    }
    const auto length = static_cast<uint32_t>(duration_us_signed < 0 ? -static_cast<int64_t>(duration_us_signed)
                                                                      : duration_us_signed);
    ++count;
    duration_us += length;
    if (shortest_us == 0 || length < shortest_us) {
        shortest_us = length;
    }
}

bool load_capture_details(const std::string& path, CaptureDetails& details, std::string& error) {
    error.clear();
    details = CaptureDetails{};

    std::ifstream input(path);
    if (!input) {
        error = "cannot read file";
        return false;
    }

    /* "Field: value" lines that are not part of every key file; some hold a long frame's extra bits. */
    std::vector<std::pair<std::string, std::string>> other_fields;

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.compare(0, 9, "RAW_Data:") == 0) {
            const char* cursor = line.c_str() + 9;
            for (;;) {
                char* end = nullptr;
                const long value = std::strtol(cursor, &end, 10);
                if (end == cursor) {
                    break;
                }
                details.pulses.add(static_cast<int32_t>(
                    std::clamp<long>(value, -2000000000L, 2000000000L)));
                cursor = end;
            }
        }
        else if (auto value = header_value(line, "Frequency"); !value.empty()) {
            details.frequency_hz = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        }
        else if (value = header_value(line, "Preset"); !value.empty()) {
            details.preset = value;
        }
        else if (value = header_value(line, "Protocol"); !value.empty()) {
            details.protocol = value;
        }
        else if (value = header_value(line, "Bit"); !value.empty()) {
            details.bits = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        }
        else if (value = header_value(line, "TE"); !value.empty()) {
            details.te_us = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        }
        else if (value = header_value(line, "Key"); !value.empty()) {
            /* Hex bytes, most significant first. */
            uint64_t key = 0;
            const char* cursor = value.c_str();
            for (int bytes = 0; bytes < 8; ++bytes) {
                char* end = nullptr;
                const unsigned long byte = std::strtoul(cursor, &end, 16);
                if (end == cursor) {
                    break;
                }
                key = (key << 8) | (byte & 0xFF);
                cursor = end;
            }
            details.key = key;
        }
        else if (const std::size_t colon = line.find(": "); colon != std::string::npos) {
            other_fields.emplace_back(line.substr(0, colon), line.substr(colon + 2));
        }
    }

    if (details.protocol.empty()) {
        error = "not a Sub-GHz file";
        return false;
    }
    details.raw = details.protocol == "RAW";

    if (const ExtraField* field = extra_field_for(details.protocol, details.bits)) {
        for (const auto& other : other_fields) {
            if (other.first != field->field) {
                continue;
            }
            uint64_t extra = 0;
            if (field->hex_bytes) {
                const char* cursor = other.second.c_str();
                for (int bytes = 0; bytes < 8; ++bytes) {
                    char* end = nullptr;
                    const unsigned long byte = std::strtoul(cursor, &end, 16);
                    if (end == cursor) {
                        break;
                    }
                    extra = (extra << 8) | (byte & 0xFF);
                    cursor = end;
                }
            }
            else {
                extra = std::strtoull(other.second.c_str(), nullptr, 10);
            }
            details.extra = extra;
            details.extra_bits = field->extra_bits;
        }
    }
    return true;
}

bool load_raw_samples(const std::string& path,
                      std::size_t max_pulses,
                      std::vector<int32_t>& samples,
                      std::string& error) {
    error.clear();
    samples.clear();

    std::ifstream input(path);
    if (!input) {
        error = "cannot read file";
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (line.compare(0, 9, "RAW_Data:") != 0) {
            continue;
        }
        const char* cursor = line.c_str() + 9;
        for (;;) {
            char* end = nullptr;
            const long value = std::strtol(cursor, &end, 10);
            if (end == cursor) {
                break;
            }
            cursor = end;
            if (value == 0) {
                continue;
            }
            if (samples.size() >= max_pulses) {
                samples.clear();
                error = "recording is too long to send";
                return false;
            }
            const auto duration = static_cast<int32_t>(std::clamp<long>(value, -2000000000L, 2000000000L));
            /* Lines are independent in the file, so two neighbours may share a level. */
            if (!samples.empty() && ((samples.back() > 0) == (duration > 0))) {
                samples.back() += duration;
            }
            else {
                samples.push_back(duration);
            }
        }
    }

    if (samples.empty()) {
        error = "no RAW data in file";
        return false;
    }
    return true;
}

bool delete_capture(const std::string& path, std::string& error) {
    error.clear();
    const std::filesystem::path file(path);
    std::error_code filesystem_error;
    /* Only ever removes a single .sub file, never a directory. */
    if (file.extension() != ".sub" || !std::filesystem::is_regular_file(file, filesystem_error)) {
        error = "not a capture file";
        return false;
    }
    if (!std::filesystem::remove(file, filesystem_error) || filesystem_error) {
        error = "cannot delete: " + filesystem_error.message();
        return false;
    }
    return true;
}

} // namespace platform
