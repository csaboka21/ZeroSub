/*
 * SPDX-License-Identifier: MIT
 *
 * The Flipper Zero .ir file format:
 *
 *   Filetype: IR signals file
 *   Version: 1
 *   #
 *   name: Power
 *   type: parsed
 *   protocol: NEC
 *   address: 04 00 00 00
 *   command: 08 00 00 00
 *   #
 *   name: Mute
 *   type: raw
 *   frequency: 38000
 *   duty_cycle: 0.330000
 *   data: 9024 4512 579 552 ...
 *
 * Address and command are four bytes each, least significant first.
 */

#include "ir_store.h"

#include "capture_store.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace platform {
namespace {

constexpr const char* kInfraredFolder = "Infrared";
constexpr const char* kSignalsFiletype = "IR signals file";
constexpr const char* kLibraryFiletype = "IR library file";

/* "key: value" -> value, or empty when the line has another key. */
bool field_value(const std::string& line, const char* key, std::string& value) {
    const std::string prefix = std::string(key) + ":";
    if (line.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    std::size_t start = prefix.size();
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
        ++start;
    }
    value = line.substr(start);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    return true;
}

/* Up to four hex bytes, least significant first. */
uint32_t parse_le_bytes(const std::string& text) {
    uint32_t value = 0;
    const char* cursor = text.c_str();
    for (int index = 0; index < 4; ++index) {
        char* end = nullptr;
        const unsigned long byte = std::strtoul(cursor, &end, 16);
        if (end == cursor) {
            break;
        }
        value |= static_cast<uint32_t>(byte & 0xFF) << (8 * index);
        cursor = end;
    }
    return value;
}

std::string le_bytes(uint32_t value) {
    char text[16];
    std::snprintf(text,
                  sizeof(text),
                  "%02X %02X %02X %02X",
                  static_cast<unsigned>(value & 0xFF),
                  static_cast<unsigned>((value >> 8) & 0xFF),
                  static_cast<unsigned>((value >> 16) & 0xFF),
                  static_cast<unsigned>((value >> 24) & 0xFF));
    return text;
}

void parse_timings(const std::string& text, std::vector<uint32_t>& timings) {
    const char* cursor = text.c_str();
    for (;;) {
        char* end = nullptr;
        const unsigned long value = std::strtoul(cursor, &end, 10);
        if (end == cursor) {
            break;
        }
        cursor = end;
        if (value == 0 || timings.size() >= kIrMaxTimings) {
            continue;
        }
        timings.push_back(static_cast<uint32_t>(value > 0xFFFFFFUL ? 0xFFFFFFUL : value));
    }
}

bool usable(const IrSignal& signal) {
    if (signal.name.empty()) {
        return false;
    }
    return signal.raw ? !signal.timings.empty() : !signal.protocol.empty();
}

bool write_signals(const std::filesystem::path& file,
                   const std::vector<IrSignal>& signals,
                   std::string& error) {
    std::ofstream output(file, std::ios::trunc);
    if (!output) {
        error = "cannot write " + file.string();
        return false;
    }

    output << "Filetype: " << kSignalsFiletype << "\n"
           << "Version: 1\n";
    for (const auto& signal : signals) {
        output << "# \n"
               << "name: " << signal.name << "\n";
        if (signal.raw) {
            char duty[24];
            std::snprintf(duty, sizeof(duty), "%.6f", static_cast<double>(signal.duty_cycle));
            output << "type: raw\n"
                   << "frequency: " << signal.frequency_hz << "\n"
                   << "duty_cycle: " << duty << "\n"
                   << "data:";
            for (const uint32_t timing : signal.timings) {
                output << ' ' << timing;
            }
            output << "\n";
        }
        else {
            output << "type: parsed\n"
                   << "protocol: " << signal.protocol << "\n"
                   << "address: " << le_bytes(signal.address) << "\n"
                   << "command: " << le_bytes(signal.command) << "\n";
        }
    }
    output.flush();
    if (!output) {
        error = "failed while writing " + file.string();
        return false;
    }
    return true;
}

} // namespace

std::string default_infrared_directory() {
    return default_module_directory(kInfraredFolder);
}

std::string ir_timestamped_name(const char* prefix) {
    const std::time_t now = std::time(nullptr);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &now);
#else
    localtime_r(&now, &local_time);
#endif
    char text[40];
    std::strftime(text, sizeof(text), "_%Y%m%d_%H%M%S", &local_time);
    return std::string(prefix) + text;
}

bool load_ir_file(const std::string& path, std::vector<IrSignal>& signals, std::string& error) {
    error.clear();
    signals.clear();

    std::ifstream input(path);
    if (!input) {
        error = "cannot read file";
        return false;
    }

    bool known_type = false;
    bool in_signal = false;
    IrSignal signal;
    const auto finish = [&]() {
        if (in_signal && usable(signal)) {
            signals.push_back(std::move(signal));
        }
        signal = IrSignal{};
        in_signal = false;
    };

    std::string line;
    std::string value;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }

        if (field_value(line, "Filetype", value)) {
            known_type = value == kSignalsFiletype || value == kLibraryFiletype;
            if (!known_type) {
                break;
            }
        }
        else if (field_value(line, "name", value)) {
            /* Every signal starts with its name. */
            finish();
            in_signal = true;
            signal.name = value;
        }
        else if (!in_signal) {
            continue;
        }
        else if (field_value(line, "type", value)) {
            signal.raw = value == "raw";
        }
        else if (field_value(line, "protocol", value)) {
            signal.protocol = value;
        }
        else if (field_value(line, "address", value)) {
            signal.address = parse_le_bytes(value);
        }
        else if (field_value(line, "command", value)) {
            signal.command = parse_le_bytes(value);
        }
        else if (field_value(line, "frequency", value)) {
            signal.frequency_hz = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        }
        else if (field_value(line, "duty_cycle", value)) {
            signal.duty_cycle = std::strtof(value.c_str(), nullptr);
        }
        else if (field_value(line, "data", value)) {
            parse_timings(value, signal.timings);
        }
    }
    finish();

    if (!known_type) {
        signals.clear();
        error = "not an infrared file";
        return false;
    }
    return true;
}

bool save_ir_file(const std::string& path, const std::vector<IrSignal>& signals, std::string& error) {
    error.clear();

    /* Written next to the file and renamed over it, so a failed write leaves the remote as it was. */
    const std::filesystem::path file(path);
    std::filesystem::path temporary = file;
    temporary += ".tmp";
    if (!write_signals(temporary, signals, error)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }

    std::error_code filesystem_error;
    std::filesystem::rename(temporary, file, filesystem_error);
    if (filesystem_error) {
        error = "cannot replace " + file.string() + ": " + filesystem_error.message();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    hand_to_invoking_user(file.string());
    return true;
}

bool save_new_ir_file(const std::string& directory,
                      const std::string& base_name,
                      const std::vector<IrSignal>& signals,
                      std::string& path,
                      std::string& error) {
    error.clear();
    path.clear();
    if (signals.empty()) {
        error = "nothing to save";
        return false;
    }
    if (!ensure_capture_directory(directory, error)) {
        return false;
    }

    const std::filesystem::path root(directory);
    std::error_code filesystem_error;
    std::filesystem::path file = root / (base_name + ".ir");
    for (int suffix = 2; std::filesystem::exists(file, filesystem_error) && suffix < 100; ++suffix) {
        file = root / (base_name + "_" + std::to_string(suffix) + ".ir");
    }

    if (!write_signals(file, signals, error)) {
        return false;
    }
    hand_to_invoking_user(file.string());
    path = file.string();
    return true;
}

std::string describe_ir_file(const std::string& path) {
    std::vector<IrSignal> signals;
    std::string error;
    if (!load_ir_file(path, signals, error)) {
        return "Not an infrared file";
    }
    if (signals.empty()) {
        return "No buttons";
    }

    std::string summary = std::to_string(signals.size()) + (signals.size() == 1 ? " button: " : " buttons: ");
    /* The label cuts the line off where it stops fitting. */
    for (std::size_t index = 0; index < signals.size() && summary.size() < 80; ++index) {
        summary += (index == 0 ? "" : ", ") + signals[index].name;
    }
    return summary;
}

bool delete_ir_file(const std::string& path, std::string& error) {
    error.clear();
    const std::filesystem::path file(path);
    std::error_code filesystem_error;
    /* Only ever removes a single .ir file, never a directory. */
    if (file.extension() != ".ir" || !std::filesystem::is_regular_file(file, filesystem_error)) {
        error = "not an infrared file";
        return false;
    }
    if (!std::filesystem::remove(file, filesystem_error) || filesystem_error) {
        error = "cannot delete: " + filesystem_error.message();
        return false;
    }
    return true;
}

} // namespace platform
