/*
 * SPDX-License-Identifier: MIT
 *
 * The 125 kHz key library: Flipper Zero .rfid files.
 *
 *   Filetype: Flipper RFID key
 *   Version: 1
 *   Key type: EM4100
 *   Data: 01 23 45 67 89
 */

#include "rfid_store.h"

#include "capture_store.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace platform {
namespace {

constexpr const char* kRfidFolder = "RFID";
constexpr const char* kRfidFiletype = "Flipper RFID key";

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

int hex_value(char letter) {
    if (letter >= '0' && letter <= '9') {
        return letter - '0';
    }
    if (letter >= 'a' && letter <= 'f') {
        return letter - 'a' + 10;
    }
    if (letter >= 'A' && letter <= 'F') {
        return letter - 'A' + 10;
    }
    return -1;
}

/* "01 23 AB" or "0123AB". */
bool parse_hex_bytes(const std::string& text, std::vector<uint8_t>& bytes) {
    bytes.clear();
    int high = -1;
    for (char letter : text) {
        if (letter == ' ' || letter == '\t') {
            continue;
        }
        const int value = hex_value(letter);
        if (value < 0) {
            return false;
        }
        if (high < 0) {
            high = value;
        }
        else {
            bytes.push_back(static_cast<uint8_t>((high << 4) | value));
            high = -1;
        }
    }
    return high < 0 && !bytes.empty();
}

std::string file_base_name(const model::RfidKey& key) {
    std::string name;
    for (char letter : key.type) {
        name += std::isalnum(static_cast<unsigned char>(letter)) != 0 ? letter : '_';
    }
    name += '_';
    for (char letter : model::rfid_hex(key.data)) {
        if (letter != ' ') {
            name += letter;
        }
    }
    return name;
}

} // namespace

std::string default_rfid_directory() {
    return default_module_directory(kRfidFolder);
}

bool load_rfid_file(const std::string& path, model::RfidKey& key, std::string& error) {
    error.clear();
    key = model::RfidKey{};
    key.type.clear();

    std::ifstream input(path);
    if (!input) {
        error = "cannot open the file";
        return false;
    }

    std::string filetype;
    bool have_data = false;
    for (std::string line; std::getline(input, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string name = trim(line.substr(0, separator));
        const std::string value = trim(line.substr(separator + 1));
        if (name == "Filetype") {
            filetype = value;
        }
        else if (name == "Key type") {
            key.type = value;
        }
        else if (name == "Data") {
            if (!parse_hex_bytes(value, key.data)) {
                error = "bad Data line";
                return false;
            }
            have_data = true;
        }
    }

    if (filetype != kRfidFiletype) {
        error = "not a Flipper RFID key";
        return false;
    }
    if (key.type.empty() || !have_data) {
        error = "key type or data missing";
        return false;
    }
    if (model::em4100_cycles_per_bit(key.type) != 0 && key.data.size() != model::kEm4100DataSize) {
        error = "EM4100 data must be 5 bytes";
        return false;
    }
    return true;
}

bool save_new_rfid_file(const std::string& directory,
                        const model::RfidKey& key,
                        std::string& path,
                        std::string& error) {
    error.clear();
    path.clear();
    if (key.type.empty() || key.data.empty()) {
        error = "nothing to save";
        return false;
    }
    if (!ensure_capture_directory(directory, error)) {
        return false;
    }

    const std::filesystem::path root(directory);
    const std::string base_name = file_base_name(key);
    std::error_code filesystem_error;
    std::filesystem::path file = root / (base_name + ".rfid");
    for (int suffix = 2; std::filesystem::exists(file, filesystem_error) && suffix < 100; ++suffix) {
        file = root / (base_name + "_" + std::to_string(suffix) + ".rfid");
    }

    {
        std::ofstream output(file, std::ios::trunc);
        if (!output) {
            error = "cannot create " + file.string();
            return false;
        }
        output << "Filetype: " << kRfidFiletype << "\n"
               << "Version: 1\n"
               << "Key type: " << key.type << "\n"
               << "Data: " << model::rfid_hex(key.data) << "\n";
        output.flush();
        if (!output) {
            error = "write failed: " + file.string();
            return false;
        }
    }
    hand_to_invoking_user(file.string());
    path = file.string();
    return true;
}

std::string describe_rfid_file(const std::string& path) {
    model::RfidKey key;
    std::string error;
    if (!load_rfid_file(path, key, error)) {
        return "Cannot be used: " + error;
    }
    return key.type + "  " + model::rfid_hex(key.data);
}

bool delete_rfid_file(const std::string& path, std::string& error) {
    error.clear();
    const std::filesystem::path file(path);
    std::error_code filesystem_error;
    /* Only ever removes a single .rfid file, never a directory. */
    if (file.extension() != ".rfid" || !std::filesystem::is_regular_file(file, filesystem_error)) {
        error = "not an RFID file";
        return false;
    }
    if (!std::filesystem::remove(file, filesystem_error) || filesystem_error) {
        error = "cannot delete: " + filesystem_error.message();
        return false;
    }
    return true;
}

} // namespace platform
