/*
 * SPDX-License-Identifier: MIT
 *
 * The Flipper Zero .nfc file format, version 4, for an NTAG / Ultralight tag:
 *
 *   Filetype: Flipper NFC device
 *   Version: 4
 *   Device type: NTAG/Ultralight
 *   UID: 04 85 92 8A A0 61 81
 *   ATQA: 00 44
 *   SAK: 00
 *   Data format version: 2
 *   NTAG/Ultralight type: NTAG215
 *   Signature: <32 bytes>
 *   Mifare version: 00 04 04 02 01 00 11 03
 *   Counter 0: 0
 *   Tearing 0: 00
 *   ...
 *   Pages total: 135
 *   Pages read: 135
 *   Page 0: 04 85 92 9B
 *   ...
 *   Failed authentication attempts: 0
 *
 * A card of which only the identity is known has device type "ISO14443-3A"
 * and ends after the SAK line.
 *
 * ATQA is written high byte first. Files of format version 2 have it low
 * byte first, and name the tag type on the "Device type" line.
 */

#include "nfc_store.h"

#include "capture_store.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace platform {
namespace {

constexpr const char* kNfcFolder = "NFC";
constexpr const char* kFiletype = "Flipper NFC device";
constexpr const char* kType2DeviceType = "NTAG/Ultralight";
constexpr const char* kUidOnlyDeviceType = "ISO14443-3A";
constexpr const char* kIso4DeviceType = "ISO14443-4A";
constexpr const char* kNfcvDeviceType = "ISO15693-3";
constexpr const char* kNfcbDeviceType = "ISO14443-3B";
constexpr const char* kSt25tbDeviceType = "ST25TB";
constexpr const char* kFelicaDeviceType = "FeliCa";
constexpr int kLsbAtqaVersion = 2;
constexpr std::size_t kMaxPages = 256;

/* "key: value" -> value; false when the line has another key. */
bool field_value(const std::string& line, const std::string& key, std::string& value) {
    if (line.size() <= key.size() || line.compare(0, key.size(), key) != 0 || line[key.size()] != ':') {
        return false;
    }
    std::size_t start = key.size() + 1;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
        ++start;
    }
    value = line.substr(start);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.pop_back();
    }
    return true;
}

std::vector<uint8_t> parse_hex_bytes(const std::string& text) {
    std::vector<uint8_t> bytes;
    const char* cursor = text.c_str();
    for (;;) {
        char* end = nullptr;
        const unsigned long value = std::strtoul(cursor, &end, 16);
        if (end == cursor || value > 0xFF) {
            break;
        }
        bytes.push_back(static_cast<uint8_t>(value));
        cursor = end;
    }
    return bytes;
}

bool any_nonzero(const uint8_t* bytes, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return true;
        }
    }
    return false;
}

/* Tag type names of the NTAG / Ultralight family, as either file version writes them. */
bool is_type2_name(const std::string& name) {
    return name.compare(0, 4, "NTAG") == 0 || name.compare(0, 17, "Mifare Ultralight") == 0;
}

std::string file_base_name(const model::NfcTag& tag) {
    std::string name = tag.type2 && !tag.type_name.empty()
                           ? tag.type_name
                           : (tag.technology == model::NfcTechnology::V
                                  ? "ISO15693"
                                  : (tag.technology == model::NfcTechnology::B
                                         ? (tag.st25tb ? "ST25TB" : "ISO14443B")
                                         : (tag.technology == model::NfcTechnology::F ? "FeliCa" : "Card")));
    for (char& character : name) {
        if (character == ' ' || character == '/') {
            character = '_';
        }
    }
    return name + "_" + model::nfc_hex(tag.uid.data(), tag.uid.size(), "");
}

/* The ATS as Flipper writes it: T0, the interface bytes T0 announces, and the historical bytes. */
void write_ats(std::ostream& output, const std::vector<uint8_t>& ats) {
    const uint8_t t0 = ats[1];
    std::size_t index = 2;
    output << "# ISO14443-4A specific data\n"
           << "T0: " << model::nfc_hex(&t0, 1) << "\n";
    const char* names[3] = {"TA(1)", "TB(1)", "TC(1)"};
    for (int bit = 0; bit < 3; ++bit) {
        if ((t0 & (0x10 << bit)) != 0 && index < ats.size()) {
            output << names[bit] << ": " << model::nfc_hex(&ats[index], 1) << "\n";
            ++index;
        }
    }
    output << "T1...Tk:";
    if (index < ats.size()) {
        output << " " << model::nfc_hex(ats.data() + index, ats.size() - index);
    }
    output << "\n";
}

/* An NFC-V tag as Flipper Zero writes ISO15693-3: system information, lock bits, blocks. */
void write_nfcv(std::ostream& output, const model::NfcTag& tag) {
    const auto yes_no = [](bool value) { return value ? "true" : "false"; };
    output << "Device type: " << kNfcvDeviceType << "\n"
           << "# UID is common for all formats\n"
           << "UID: " << model::nfc_hex(tag.uid.data(), tag.uid.size()) << "\n"
           << "# ISO15693-3 specific data\n";
    if (tag.has_dsfid) {
        output << "DSFID: " << model::nfc_hex(&tag.dsfid, 1) << "\n";
    }
    if (tag.has_afi) {
        output << "AFI: " << model::nfc_hex(&tag.afi, 1) << "\n";
    }
    if (tag.has_ic_ref) {
        output << "IC Reference: " << model::nfc_hex(&tag.ic_ref, 1) << "\n";
    }
    output << "Lock DSFID: " << yes_no(tag.lock_dsfid) << "\n"
           << "Lock AFI: " << yes_no(tag.lock_afi) << "\n";
    if (tag.block_count != 0 && tag.block_size != 0) {
        output << "Block Count: " << tag.block_count << "\n"
               << "Block Size: " << model::nfc_hex(&tag.block_size, 1) << "\n"
               << "Data Content: " << model::nfc_hex(tag.data.data(), tag.data.size()) << "\n"
               << "# Block Security Status: 01 = locked, 00 = not locked\n"
               << "Security Status: " << model::nfc_hex(tag.block_security.data(), tag.block_security.size())
               << "\n";
    }
}

/* NFC-B as Flipper Zero writes it: ISO14443-3B (PUPI, application data, protocol info) or ST25TB (blocks). */
void write_nfcb(std::ostream& output, const model::NfcTag& tag) {
    output << "Device type: " << (tag.st25tb ? kSt25tbDeviceType : kNfcbDeviceType) << "\n"
           << "# UID is common for all formats\n"
           << "UID: " << model::nfc_hex(tag.uid.data(), tag.uid.size()) << "\n";
    if (!tag.st25tb) {
        output << "# ISO14443-3B specific data\n"
               << "Application data: " << model::nfc_hex(tag.app_data.data(), tag.app_data.size()) << "\n"
               << "Protocol info: " << model::nfc_hex(tag.protocol_info.data(), tag.protocol_info.size()) << "\n";
        return;
    }
    output << "# ST25TB specific data\n"
           << "ST25TB Type: " << tag.st25tb_type << "\n";
    for (std::size_t block = 0; block < tag.block_count && (block + 1) * 4 <= tag.data.size(); ++block) {
        output << "Block " << block << ": " << model::nfc_hex(tag.data.data() + block * 4, 4) << "\n";
    }
    output << "System OTP Block: " << model::nfc_hex(tag.st25tb_system.data(), tag.st25tb_system.size()) << "\n";
}

std::string hex4(uint16_t value) {
    char text[8];
    std::snprintf(text, sizeof(text), "%04X", static_cast<unsigned>(value));
    return text;
}

std::string hex3(uint16_t value) {
    char text[8];
    std::snprintf(text, sizeof(text), "%03X", static_cast<unsigned>(value));
    return text;
}

/* FeliCa as Flipper Zero writes it (data format 4): Lite blocks, or a Standard card's systems. */
void write_felica(std::ostream& output, const model::NfcTag& tag) {
    output << "Device type: " << kFelicaDeviceType << "\n"
           << "# UID is common for all formats\n"
           << "UID: " << model::nfc_hex(tag.uid.data(), tag.uid.size()) << "\n"
           << "# FeliCa specific data\n"
           << "Data format version: 4\n"
           << "Manufacture id: " << model::nfc_hex(tag.uid.data(), tag.uid.size()) << "\n"
           << "Manufacture parameter: " << model::nfc_hex(tag.felica_pmm.data(), tag.felica_pmm.size()) << "\n"
           << "IC Type: " << model::felica_ic_name(tag.felica_pmm) << "\n\n";
    if (tag.felica_lite) {
        output << "# Felica Lite specific data\n"
               << "Blocks total: " << model::kFelicaLiteBlocks << "\n"
               << "Blocks read: " << tag.blocks_read << "\n";
        for (std::size_t block = 0; block < model::kFelicaLiteBlocks; ++block) {
            const std::size_t offset = block * model::kFelicaLiteRecord;
            if (offset + model::kFelicaLiteRecord > tag.data.size()) {
                break;
            }
            output << "Block " << block << ": " << model::nfc_hex(tag.data.data() + offset, model::kFelicaLiteRecord)
                   << "\n";
        }
        return;
    }
    output << "# Felica Standard specific data\n"
           << "System found: " << tag.felica_systems.size() << "\n";
    for (std::size_t index = 0; index < tag.felica_systems.size(); ++index) {
        const auto& system = tag.felica_systems[index];
        char key[16];
        std::snprintf(key, sizeof(key), "System %02X", static_cast<unsigned>(index));
        /* Key versions are only known after REQUEST SERVICE, which is not sent: FFFF, as Flipper writes unknown. */
        output << "\n\n" << key << ": " << hex4(system.code) << " | Key version FFFF |\n\n"
               << "Area found: " << system.areas.size() << "\n";
        for (std::size_t area = 0; area < system.areas.size(); ++area) {
            const auto& a = system.areas[area];
            output << "Area " << hex3(static_cast<uint16_t>(area)) << ": | Code " << hex4(a.code) << " | End "
                   << hex4(a.end) << " | Services #" << hex3(a.first) << "-#" << hex3(a.last)
                   << " | Key version FFFF |\n";
        }
        output << "\nService found: " << system.services.size() << "\n";
        for (std::size_t service = 0; service < system.services.size(); ++service) {
            const uint16_t code = system.services[service];
            char attribute[4];
            std::snprintf(attribute, sizeof(attribute), "%02X", static_cast<unsigned>(code & 0x3F));
            output << "Service " << hex3(static_cast<uint16_t>(service)) << ": | Code " << hex4(code) << " | Attrib. "
                   << attribute << " | " << (model::felica_service_public(code) ? "Public " : "Private")
                   << " | Key version FFFF |\n";
        }
        output << "\nPublic blocks read: " << system.blocks.size() << "\n";
        for (std::size_t block = 0; block < system.blocks.size(); ++block) {
            const auto& b = system.blocks[block];
            char index_text[4];
            std::snprintf(index_text, sizeof(index_text), "%02X", static_cast<unsigned>(b.index));
            output << "Block " << hex4(static_cast<uint16_t>(block)) << ": | Service code " << hex4(b.service)
                   << " | Block index " << index_text << " | Data: " << model::nfc_hex(b.data.data(), b.data.size())
                   << " |\n";
        }
    }
}

/* Reads back what write_felica() and Flipper Zero write; lines that do not fit are skipped. */
void parse_felica(const std::vector<std::string>& lines, model::NfcTag& tag) {
    model::FelicaSystem* system = nullptr;
    bool lite_section = false;
    std::vector<uint8_t> lite(model::kFelicaLiteBlocks * model::kFelicaLiteRecord, 0);
    std::size_t lite_read = 0;
    for (const auto& line : lines) {
        std::string value;
        unsigned a = 0, b = 0, c = 0, d = 0, e = 0;
        if (field_value(line, "Manufacture parameter", value)) {
            const auto bytes = parse_hex_bytes(value);
            if (bytes.size() == 8) {
                std::copy(bytes.begin(), bytes.end(), tag.felica_pmm.begin());
            }
        }
        else if (field_value(line, "Blocks total", value)) {
            lite_section = true;
        }
        else if (field_value(line, "Blocks read", value)) {
            lite_read = static_cast<std::size_t>(std::strtoul(value.c_str(), nullptr, 10));
        }
        else if (std::sscanf(line.c_str(), "System %x: %x", &a, &b) == 2) {
            tag.felica_systems.emplace_back();
            system = &tag.felica_systems.back();
            system->code = static_cast<uint16_t>(b);
        }
        else if (system && std::sscanf(line.c_str(), "Area %x: | Code %x | End %x | Services #%x-#%x", &a, &b, &c, &d,
                                       &e) == 5) {
            system->areas.push_back({static_cast<uint16_t>(b), static_cast<uint16_t>(c), static_cast<uint16_t>(d),
                                     static_cast<uint16_t>(e)});
        }
        else if (system && std::sscanf(line.c_str(), "Area %x: | Code %x | Services #%x-#%x", &a, &b, &d, &e) == 4) {
            /* Data format 2 had no end code. */
            system->areas.push_back({static_cast<uint16_t>(b), 0xFFFF, static_cast<uint16_t>(d),
                                     static_cast<uint16_t>(e)});
        }
        else if (system && std::sscanf(line.c_str(), "Service %x: | Code %x", &a, &b) == 2) {
            system->services.push_back(static_cast<uint16_t>(b));
        }
        else if (system && std::sscanf(line.c_str(), "Block %x: | Service code %x | Block index %x", &a, &b, &c) == 3) {
            const std::size_t data_at = line.find("Data: ");
            if (data_at != std::string::npos) {
                const auto bytes = parse_hex_bytes(line.substr(data_at + 6));
                if (bytes.size() >= model::kFelicaBlockSize) {
                    model::FelicaPublicBlock block;
                    block.service = static_cast<uint16_t>(b);
                    block.index = static_cast<uint8_t>(c);
                    std::copy_n(bytes.begin(), model::kFelicaBlockSize, block.data.begin());
                    system->blocks.push_back(block);
                }
            }
        }
        else if (lite_section && line.compare(0, 6, "Block ") == 0) {
            char* end = nullptr;
            const unsigned long block = std::strtoul(line.c_str() + 6, &end, 10);
            if (end && *end == ':' && block < model::kFelicaLiteBlocks) {
                const auto bytes = parse_hex_bytes(end + 1);
                if (bytes.size() == model::kFelicaLiteRecord) {
                    std::copy(bytes.begin(), bytes.end(), lite.begin() + static_cast<std::ptrdiff_t>(block * model::kFelicaLiteRecord));
                }
            }
        }
    }
    if (lite_section || model::felica_is_lite(tag.felica_pmm)) {
        tag.felica_lite = true;
        tag.felica_systems.clear();
        tag.block_count = model::kFelicaLiteBlocks;
        tag.block_size = model::kFelicaBlockSize;
        tag.blocks_read = static_cast<uint16_t>(std::min(lite_read, model::kFelicaLiteBlocks));
        tag.data = std::move(lite);
    }
}

bool write_tag(const std::filesystem::path& file, const model::NfcTag& tag, std::string& error) {
    std::ofstream output(file, std::ios::trunc);
    if (!output) {
        error = "cannot write " + file.string();
        return false;
    }
    if (tag.technology != model::NfcTechnology::A) {
        output << "Filetype: " << kFiletype << "\n"
               << "Version: 4\n"
               << "# Saved by ZeroSub\n";
        if (tag.technology == model::NfcTechnology::V) {
            write_nfcv(output, tag);
        }
        else if (tag.technology == model::NfcTechnology::F) {
            write_felica(output, tag);
        }
        else {
            write_nfcb(output, tag);
        }
        output.flush();
        if (!output) {
            error = "write failed: " + file.string();
            return false;
        }
        return true;
    }

    const uint8_t atqa_high_first[2] = {tag.atqa[1], tag.atqa[0]};
    output << "Filetype: " << kFiletype << "\n"
           << "Version: 4\n"
           << "# Saved by ZeroSub\n"
           << "Device type: "
           << (tag.type2 ? kType2DeviceType : (tag.ats.size() >= 2 ? kIso4DeviceType : kUidOnlyDeviceType)) << "\n"
           << "# UID is common for all formats\n"
           << "UID: " << model::nfc_hex(tag.uid.data(), tag.uid.size()) << "\n"
           << "# ISO14443-3A specific data\n"
           << "ATQA: " << model::nfc_hex(atqa_high_first, 2) << "\n"
           << "SAK: " << model::nfc_hex(&tag.sak, 1) << "\n";
    if (!tag.type2 && tag.ats.size() >= 2) {
        write_ats(output, tag.ats);
    }

    if (tag.type2) {
        output << "# NTAG/Ultralight specific data\n"
               << "Data format version: 2\n"
               << "NTAG/Ultralight type: " << tag.type_name << "\n"
               << "Signature: " << model::nfc_hex(tag.signature.data(), tag.signature.size()) << "\n"
               << "Mifare version: " << model::nfc_hex(tag.version.data(), tag.version.size()) << "\n";
        /* A file without these lines does not load on a Flipper. */
        for (std::size_t counter = 0; counter < 3; ++counter) {
            output << "Counter " << counter << ": " << tag.counters[counter] << "\n"
                   << "Tearing " << counter << ": " << model::nfc_hex(&tag.tearing[counter], 1) << "\n";
        }
        output << "Pages total: " << tag.pages_total << "\n"
               << "Pages read: " << tag.pages_read << "\n";
        for (std::size_t page = 0; page < tag.pages_total; ++page) {
            const std::size_t offset = page * model::kNfcPageSize;
            if (offset + model::kNfcPageSize > tag.data.size()) {
                break;
            }
            output << "Page " << page << ": " << model::nfc_hex(tag.data.data() + offset, model::kNfcPageSize)
                   << "\n";
        }
        output << "Failed authentication attempts: 0\n";
    }

    output.flush();
    if (!output) {
        error = "write failed: " + file.string();
        return false;
    }
    return true;
}

} // namespace

std::string default_nfc_directory() {
    return default_module_directory(kNfcFolder);
}

bool load_nfc_file(const std::string& path, model::NfcTag& tag, std::string& error) {
    error.clear();
    tag = model::NfcTag{};

    std::ifstream input(path);
    if (!input) {
        error = "cannot open the file";
        return false;
    }

    std::string filetype;
    std::string device_type;
    std::string type_name;
    int version = 0;
    bool have_atqa = false;
    bool have_sak = false;
    std::vector<uint8_t> atqa;
    std::size_t pages_total = 0;
    std::size_t pages_read = 0;
    bool have_pages_read = false;
    std::vector<uint8_t> data;
    std::array<uint32_t, 3> counters{};
    std::array<uint8_t, 3> tearing{{0xBD, 0xBD, 0xBD}};
    /* ATS parts: T0, then TA(1), TB(1), TC(1) when present, then the historical bytes. */
    bool have_t0 = false;
    uint8_t t0 = 0;
    std::array<int, 3> interface_bytes{{-1, -1, -1}};
    std::vector<uint8_t> historical;
    /* NFC-V (ISO15693-3 / SLIX files). */
    model::NfcTag nfcv;
    uint32_t block_count = 0;
    /* NFC-B (ISO14443-3B / ST25TB files). */
    model::NfcTag nfcb;
    std::vector<uint8_t> st25tb_blocks;
    /* FeliCa files: their own layout, parsed after the header. */
    std::vector<std::string> felica_lines;

    std::string line;
    std::string value;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (device_type == kFelicaDeviceType && line.compare(0, 4, "UID:") != 0) {
            felica_lines.push_back(line);
            continue;
        }
        if (field_value(line, "Filetype", value)) {
            filetype = value;
        }
        else if (field_value(line, "Version", value)) {
            version = std::atoi(value.c_str());
        }
        else if (field_value(line, "Device type", value)) {
            device_type = value;
        }
        else if (field_value(line, "NTAG/Ultralight type", value)) {
            type_name = value;
        }
        else if (field_value(line, "UID", value)) {
            tag.uid = parse_hex_bytes(value);
        }
        else if (field_value(line, "ATQA", value)) {
            atqa = parse_hex_bytes(value);
            have_atqa = atqa.size() == 2;
        }
        else if (field_value(line, "SAK", value)) {
            const auto bytes = parse_hex_bytes(value);
            have_sak = bytes.size() == 1;
            if (have_sak) {
                tag.sak = bytes[0];
            }
        }
        else if (field_value(line, "Signature", value)) {
            const auto bytes = parse_hex_bytes(value);
            if (bytes.size() == tag.signature.size()) {
                std::copy(bytes.begin(), bytes.end(), tag.signature.begin());
            }
        }
        else if (field_value(line, "Mifare version", value)) {
            const auto bytes = parse_hex_bytes(value);
            if (bytes.size() == tag.version.size()) {
                std::copy(bytes.begin(), bytes.end(), tag.version.begin());
            }
        }
        else if (field_value(line, "Pages total", value)) {
            pages_total = static_cast<std::size_t>(std::strtoul(value.c_str(), nullptr, 10));
        }
        else if (field_value(line, "Pages read", value)) {
            pages_read = static_cast<std::size_t>(std::strtoul(value.c_str(), nullptr, 10));
            have_pages_read = true;
        }
        else if (line.compare(0, 8, "Counter ") == 0 || line.compare(0, 8, "Tearing ") == 0) {
            const bool is_counter = line[0] == 'C';
            char* end = nullptr;
            const unsigned long number = std::strtoul(line.c_str() + 8, &end, 10);
            if (end && *end == ':' && number < 3) {
                if (is_counter) {
                    counters[number] = static_cast<uint32_t>(std::strtoul(end + 1, nullptr, 10) & 0xFFFFFF);
                }
                else {
                    const auto bytes = parse_hex_bytes(end + 1);
                    if (bytes.size() == 1) {
                        tearing[number] = bytes[0];
                    }
                }
            }
        }
        else if (field_value(line, "T0", value)) {
            const auto bytes = parse_hex_bytes(value);
            have_t0 = bytes.size() == 1;
            t0 = have_t0 ? bytes[0] : 0;
        }
        else if (field_value(line, "TA(1)", value) || field_value(line, "TB(1)", value) ||
                 field_value(line, "TC(1)", value)) {
            const auto bytes = parse_hex_bytes(value);
            if (bytes.size() == 1) {
                interface_bytes[static_cast<std::size_t>(line[1] - 'A')] = bytes[0];
            }
        }
        else if (field_value(line, "T1...Tk", value)) {
            historical = parse_hex_bytes(value);
        }
        else if (field_value(line, "DSFID", value) || field_value(line, "AFI", value) ||
                 field_value(line, "IC Reference", value)) {
            const auto bytes = parse_hex_bytes(value);
            if (bytes.size() == 1) {
                if (line[0] == 'D') {
                    nfcv.has_dsfid = true;
                    nfcv.dsfid = bytes[0];
                }
                else if (line[0] == 'A') {
                    nfcv.has_afi = true;
                    nfcv.afi = bytes[0];
                }
                else {
                    nfcv.has_ic_ref = true;
                    nfcv.ic_ref = bytes[0];
                }
            }
        }
        else if (field_value(line, "Lock DSFID", value)) {
            nfcv.lock_dsfid = value == "true";
        }
        else if (field_value(line, "Lock AFI", value)) {
            nfcv.lock_afi = value == "true";
        }
        else if (field_value(line, "Application data", value)) {
            const auto bytes = parse_hex_bytes(value);
            if (bytes.size() == nfcb.app_data.size()) {
                std::copy(bytes.begin(), bytes.end(), nfcb.app_data.begin());
            }
        }
        else if (field_value(line, "Protocol info", value)) {
            const auto bytes = parse_hex_bytes(value);
            if (bytes.size() == nfcb.protocol_info.size()) {
                std::copy(bytes.begin(), bytes.end(), nfcb.protocol_info.begin());
            }
        }
        else if (field_value(line, "ST25TB Type", value)) {
            nfcb.st25tb_type = value;
        }
        else if (field_value(line, "System OTP Block", value)) {
            const auto bytes = parse_hex_bytes(value);
            if (bytes.size() == 4) {
                std::copy(bytes.begin(), bytes.end(), nfcb.st25tb_system.begin());
            }
        }
        else if (line.compare(0, 6, "Block ") == 0 && line.size() > 6 && line[6] >= '0' && line[6] <= '9') {
            char* end = nullptr;
            const unsigned long block = std::strtoul(line.c_str() + 6, &end, 10);
            if (end && *end == ':' && block < 256) {
                const auto bytes = parse_hex_bytes(end + 1);
                if (bytes.size() == 4) {
                    if (st25tb_blocks.size() < (block + 1) * 4) {
                        st25tb_blocks.resize((block + 1) * 4, 0);
                    }
                    std::copy(bytes.begin(), bytes.end(), st25tb_blocks.begin() + static_cast<std::ptrdiff_t>(block * 4));
                }
            }
        }
        else if (field_value(line, "Block Count", value)) {
            block_count = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        }
        else if (field_value(line, "Block Size", value)) {
            const auto bytes = parse_hex_bytes(value);
            nfcv.block_size = bytes.size() == 1 ? bytes[0] : 0;
        }
        else if (field_value(line, "Data Content", value)) {
            nfcv.data = parse_hex_bytes(value);
        }
        else if (field_value(line, "Security Status", value)) {
            nfcv.block_security = parse_hex_bytes(value);
        }
        else if (line.compare(0, 5, "Page ") == 0) {
            char* end = nullptr;
            const unsigned long page = std::strtoul(line.c_str() + 5, &end, 10);
            if (end && *end == ':' && page < kMaxPages) {
                const auto bytes = parse_hex_bytes(end + 1);
                if (bytes.size() == model::kNfcPageSize) {
                    if (data.size() < (page + 1) * model::kNfcPageSize) {
                        data.resize((page + 1) * model::kNfcPageSize, 0);
                    }
                    std::copy(bytes.begin(), bytes.end(), data.begin() + static_cast<std::ptrdiff_t>(page * model::kNfcPageSize));
                }
            }
        }
    }

    if (filetype != kFiletype) {
        error = "not a Flipper NFC file";
        return false;
    }
    if (device_type == kFelicaDeviceType) {
        if (tag.uid.size() != 8) {
            error = "the file has no valid IDm";
            return false;
        }
        model::NfcTag felica;
        felica.technology = model::NfcTechnology::F;
        felica.uid = tag.uid;
        parse_felica(felica_lines, felica);
        tag = std::move(felica);
        return true;
    }
    if (device_type == kSt25tbDeviceType || device_type == kNfcbDeviceType || device_type == "ISO14443-4B") {
        nfcb.technology = model::NfcTechnology::B;
        nfcb.uid = tag.uid;
        if (device_type == kSt25tbDeviceType) {
            const model::St25tbSpec* spec = model::nfc_st25tb_spec_by_name(nfcb.st25tb_type);
            if (tag.uid.size() != 8 || !spec) {
                error = "not a known ST25TB file";
                return false;
            }
            nfcb.st25tb = true;
            nfcb.block_count = spec->blocks;
            nfcb.block_size = 4;
            nfcb.blocks_read = static_cast<uint16_t>(std::min<std::size_t>(st25tb_blocks.size() / 4, spec->blocks));
            st25tb_blocks.resize(static_cast<std::size_t>(spec->blocks) * 4, 0);
            nfcb.data = std::move(st25tb_blocks);
        }
        else if (tag.uid.size() != 4) {
            error = "the file has no valid PUPI";
            return false;
        }
        tag = std::move(nfcb);
        return true;
    }
    if (device_type.compare(0, 8, "ISO15693") == 0 || device_type.compare(0, 4, "SLIX") == 0) {
        /* NFC-V: an 8-byte UID, and the memory as one run of blocks. */
        if (tag.uid.size() != 8) {
            error = "the file has no valid UID";
            return false;
        }
        nfcv.technology = model::NfcTechnology::V;
        nfcv.uid = tag.uid;
        if (block_count > 256 || nfcv.block_size > 32) {
            block_count = 0;
        }
        nfcv.block_count = static_cast<uint16_t>(block_count);
        const std::size_t bytes = static_cast<std::size_t>(nfcv.block_count) * nfcv.block_size;
        nfcv.blocks_read = nfcv.data.size() >= bytes ? nfcv.block_count
                                                      : static_cast<uint16_t>(nfcv.block_size ? nfcv.data.size() / nfcv.block_size : 0);
        nfcv.data.resize(bytes, 0);
        nfcv.block_security.resize(nfcv.block_count, 0);
        tag = std::move(nfcv);
        return true;
    }
    if (!have_atqa || !have_sak) {
        error = "not an NFC-A card (" + (device_type.empty() ? std::string("unknown type") : device_type) + ")";
        return false;
    }
    if (tag.uid.size() != 4 && tag.uid.size() != 7 && tag.uid.size() != 10) {
        error = "the file has no valid UID";
        return false;
    }
    if (have_t0) {
        /* TL counts itself: TL, T0, the interface bytes T0 announces, the historical bytes. */
        tag.ats.assign(1, 0);
        tag.ats.push_back(t0);
        for (std::size_t bit = 0; bit < 3; ++bit) {
            if ((t0 & (0x10 << bit)) != 0) {
                tag.ats.push_back(static_cast<uint8_t>(interface_bytes[bit] < 0 ? 0 : interface_bytes[bit]));
            }
        }
        tag.ats.insert(tag.ats.end(), historical.begin(), historical.end());
        tag.ats[0] = static_cast<uint8_t>(tag.ats.size());
    }
    if (version > kLsbAtqaVersion) {
        tag.atqa = {atqa[1], atqa[0]};
    }
    else {
        tag.atqa = {atqa[0], atqa[1]};
    }

    /* Older files name the tag type where newer ones name the family. */
    if (type_name.empty() && device_type != kType2DeviceType && is_type2_name(device_type)) {
        type_name = device_type;
    }
    const bool type2 = device_type == kType2DeviceType || !type_name.empty();
    if (type2 && pages_total >= 4 && pages_total <= kMaxPages && !data.empty()) {
        tag.type2 = true;
        tag.type_name = type_name.empty() ? model::nfc_type2_name_for_pages(static_cast<uint16_t>(pages_total))
                                          : type_name;
        tag.pages_total = static_cast<uint16_t>(pages_total);
        tag.pages_read = static_cast<uint16_t>(have_pages_read ? std::min(pages_read, pages_total) : pages_total);
        data.resize(pages_total * model::kNfcPageSize, 0);
        tag.data = std::move(data);
        tag.has_version = any_nonzero(tag.version.data(), tag.version.size());
        tag.has_signature = any_nonzero(tag.signature.data(), tag.signature.size());
        tag.counters = counters;
        tag.tearing = tearing;
    }
    else {
        tag.version.fill(0);
        tag.signature.fill(0);
    }
    return true;
}

bool save_new_nfc_file(const std::string& directory,
                       const model::NfcTag& tag,
                       std::string& path,
                       std::string& error) {
    error.clear();
    path.clear();
    if (tag.uid.empty()) {
        error = "nothing to save";
        return false;
    }
    if (!ensure_capture_directory(directory, error)) {
        return false;
    }

    const std::filesystem::path root(directory);
    const std::string base_name = file_base_name(tag);
    std::error_code filesystem_error;
    std::filesystem::path file = root / (base_name + ".nfc");
    for (int suffix = 2; std::filesystem::exists(file, filesystem_error) && suffix < 100; ++suffix) {
        file = root / (base_name + "_" + std::to_string(suffix) + ".nfc");
    }

    if (!write_tag(file, tag, error)) {
        return false;
    }
    hand_to_invoking_user(file.string());
    path = file.string();
    return true;
}

std::string describe_nfc_file(const std::string& path) {
    model::NfcTag tag;
    std::string error;
    if (!load_nfc_file(path, tag, error)) {
        return "Cannot be used: " + error;
    }
    return model::nfc_family_name(tag) + "  " + model::nfc_hex(tag.uid.data(), tag.uid.size());
}

bool delete_nfc_file(const std::string& path, std::string& error) {
    error.clear();
    const std::filesystem::path file(path);
    std::error_code filesystem_error;
    /* Only ever removes a single .nfc file, never a directory. */
    if (file.extension() != ".nfc" || !std::filesystem::is_regular_file(file, filesystem_error)) {
        error = "not an NFC file";
        return false;
    }
    if (!std::filesystem::remove(file, filesystem_error) || filesystem_error) {
        error = "cannot delete: " + filesystem_error.message();
        return false;
    }
    return true;
}

bool load_amiibo_key_file(const std::string& directory, std::vector<uint8_t>& raw, std::string& error) {
    error.clear();
    raw.clear();
    constexpr std::size_t kKeySize = 160;
    for (const char* name : {"key_retail.bin", "retail.bin"}) {
        const std::filesystem::path file = std::filesystem::path(directory) / name;
        std::error_code filesystem_error;
        if (!std::filesystem::is_regular_file(file, filesystem_error)) {
            continue;
        }
        std::ifstream input(file, std::ios::binary);
        if (!input) {
            error = std::string("cannot read ") + name;
            return false;
        }
        raw.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        if (raw.size() != kKeySize) {
            error = std::string(name) + " must be 160 bytes, not " + std::to_string(raw.size());
            raw.clear();
            return false;
        }
        return true;
    }
    error = "no key_retail.bin in the NFC folder";
    return false;
}

} // namespace platform
